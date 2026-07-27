#include "DynamicBand.h"

#include <cmath>

namespace
{
    // Deliberate bit-exact comparison for the coefficient memoisation below.
    // An epsilon would be wrong here in both directions: it would keep stale
    // coefficients alive for gains that genuinely moved by less than the
    // epsilon (a slow fade would quantise), and it buys nothing, because the
    // whole point of the cache is to catch the *exactly unchanged* case of a
    // band sitting at a settled gain. Wrapped so the intent is stated once
    // rather than suppressed at three call sites.
    template <typename FloatType>
    bool isExactlyUnchanged (FloatType a, FloatType b) noexcept
    {
        JUCE_BEGIN_IGNORE_WARNINGS_GCC_LIKE ("-Wfloat-equal")
        return a == b;
        JUCE_END_IGNORE_WARNINGS_GCC_LIKE
    }
}

DynamicBand::DynamicBand (ShelfDirection shelfDirectionToUse) noexcept
    : shelfDirection (shelfDirectionToUse)
{
}

float DynamicBand::onePoleCoefficient (double timeConstantSeconds, double stepsPerSecond) noexcept
{
    if (timeConstantSeconds <= 0.0 || stepsPerSecond <= 0.0)
        return 1.0f;

    return static_cast<float> (1.0 - std::exp (-1.0 / (timeConstantSeconds * stepsPerSecond)));
}

void DynamicBand::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate;

    const auto numChannels = static_cast<int> (spec.numChannels);

    svf.prepare (numChannels);
    saturator.prepare (numChannels);
    detector.prepare (spec);

    // The static-gain smoother steps once per sample; the frequency/Q and
    // Gain/Q ramps step once per sub-block (see LancetEngine's 32-sample
    // granularity, mirrored here so a band driven directly by a test with a
    // different chunk size still ramps at approximately the same rate).
    staticGainCoefficient = onePoleCoefficient (staticGainSmoothingSeconds, sampleRate);
    subBlockRampCoefficient = onePoleCoefficient (coefficientRampSeconds, sampleRate / 32.0);
    gainQCouplingCoefficient = onePoleCoefficient (gainQCouplingSeconds, sampleRate);

    // Prime every smoother at its current target so the very first
    // processSubBlock() call runs at the settings the host/session actually
    // asked for, rather than gliding up to them from a default.
    staticGainDbSmoothed = staticGainDb;
    frequencyHzSmoothed = frequencyHz;
    qSmoothed = effectiveQ();
    dynamicGainDbAbsSmoothed = 0.0f;

    reset();

    // Prime the detector's ballistics, bandpass and the SVF's frequency warp
    // immediately so the very first processSubBlock() call runs with correct
    // values. Attack/Release have to be re-pushed here specifically because
    // their coefficients are derived from the sample rate, which only became
    // known a few lines ago - and they are re-pushed from this band's own
    // remembered values, not from a hardcoded pair (see setAttackMs()).
    detector.setAttackMs (attackMs);
    detector.setReleaseMs (releaseMs);
    detector.setFrequencyAndQ (frequencyHzSmoothed, qSmoothed);
    detectorCoefficientFrequencyHz = frequencyHzSmoothed;
    detectorCoefficientQ = qSmoothed;

    gBase = lnct::TptSvf::computeGBase (static_cast<double> (frequencyHzSmoothed), sampleRate);
    gBaseFrequencyHz = frequencyHzSmoothed;
}

void DynamicBand::reset()
{
    svf.reset();
    saturator.reset();
    detector.reset();

    staticGainDbSmoothed = staticGainDb;
    frequencyHzSmoothed = frequencyHz;
    qSmoothed = effectiveQ();
    dynamicGainDbAbsSmoothed = 0.0f;

    coefficientCacheValid = false;
    lastAppliedDynamicGainDb.store (0.0f, std::memory_order_relaxed);
    lastAppliedFilterQ.store (effectiveQ(), std::memory_order_relaxed);
}

lnct::TptSvf::Type DynamicBand::currentSvfType() const noexcept
{
    if (! isEffectivelyShelf())
        return lnct::TptSvf::Type::bell;

    return shelfDirection == ShelfDirection::low ? lnct::TptSvf::Type::lowShelf
                                                 : lnct::TptSvf::Type::highShelf;
}

float DynamicBand::computeKneeWidthDb() const noexcept
{
    return juce::jlimit (kneeWidthFloorDb, kneeWidthCeilingDb, std::abs (rangeDb) * kneeWidthRangeSlope);
}

float DynamicBand::computeMainFilterQ (float dynamicGainDbAbs) const noexcept
{
    const auto baseQ = qSmoothed;

    if (! gainQEnabled || rangeDb == 0.0f)
        return baseQ;

    const auto fraction = juce::jlimit (0.0f, 1.0f, dynamicGainDbAbs / std::abs (rangeDb));
    const auto qMultiplier = juce::jmap (fraction, 1.0f, gainQMinMultiplier);
    return baseQ * qMultiplier;
}

float DynamicBand::softKneeOvershoot (float overshootDb, float kneeWidthDb) noexcept
{
    if (2.0f * overshootDb <= -kneeWidthDb)
        return 0.0f;

    if (2.0f * std::abs (overshootDb) < kneeWidthDb)
    {
        const auto shifted = overshootDb + kneeWidthDb * 0.5f;
        return (shifted * shifted) / (2.0f * kneeWidthDb);
    }

    return overshootDb;
}

float DynamicBand::computeSaturationDrive (float positiveGainDb) const noexcept
{
    const auto fraction = juce::jlimit (0.0f, 1.0f, positiveGainDb / saturationGainReferenceDb);
    return juce::jmap (fraction, saturationDriveFloor, saturationDriveCeiling);
}

void DynamicBand::processSubBlock (juce::dsp::AudioBlock<float> mainSubBlock,
                                    const juce::dsp::AudioBlock<const float>& preChainBlock,
                                    size_t startSample,
                                    size_t numSamples) noexcept
{
    processSubBlock (mainSubBlock, preChainBlock, juce::dsp::AudioBlock<const float>(), startSample, numSamples);
}

void DynamicBand::processSubBlock (juce::dsp::AudioBlock<float> mainSubBlock,
                                    const juce::dsp::AudioBlock<const float>& preChainBlock,
                                    const juce::dsp::AudioBlock<const float>& sidechainBlock,
                                    size_t startSample,
                                    size_t numSamples) noexcept
{
    if (numSamples == 0)
        return;

    //==========================================================================
    // Once-per-sub-block work.

    // Frequency/Q ramp (see the class comment): the SVF tolerates stepped
    // coefficient updates, so this only shapes audibility of knob moves.
    frequencyHzSmoothed += (frequencyHz - frequencyHzSmoothed) * subBlockRampCoefficient;
    qSmoothed += (effectiveQ() - qSmoothed) * subBlockRampCoefficient;

    // Dirty-flagged coefficient recompute (SOTA brief F8): a band whose
    // frequency/Q are not being automated recomputes neither the detector's
    // bandpass (one makeBandPass call) nor the SVF's tan() warp. Both were
    // unconditional per sub-block up to v0.3.0.
    if (std::abs (frequencyHzSmoothed - detectorCoefficientFrequencyHz) > frequencyDirtyEpsilonHz
        || std::abs (qSmoothed - detectorCoefficientQ) > qDirtyEpsilon)
    {
        detector.setFrequencyAndQ (frequencyHzSmoothed, qSmoothed);
        detectorCoefficientFrequencyHz = frequencyHzSmoothed;
        detectorCoefficientQ = qSmoothed;
    }

    if (std::abs (frequencyHzSmoothed - gBaseFrequencyHz) > frequencyDirtyEpsilonHz)
    {
        gBase = lnct::TptSvf::computeGBase (static_cast<double> (frequencyHzSmoothed), sampleRate);
        gBaseFrequencyHz = frequencyHzSmoothed;
    }

    // External sidechain (SOTA brief F3): fall back to the pre-chain tap
    // whenever the host gives us no sidechain channels, so selecting
    // External in a host that has no sidechain routing (or with the bus
    // disabled) is inaudible rather than silent. The envelope state is never
    // reset when the source changes, so switching mid-play does not click.
    const auto haveSidechain = sidechainBlock.getNumChannels() > 0 && sidechainBlock.getNumSamples() >= startSample + numSamples;
    const auto& detectorSource = (sidechainExternal && haveSidechain) ? sidechainBlock : preChainBlock;

    detector.setSplitMode (! detectorWide);

    const auto svfType = currentSvfType();

    // Gain/Q coupling (SOTA brief F6) is evaluated once per sub-block from
    // the 30 ms-smoothed dynamic gain the previous samples left behind, so
    // the realised Q can only move by a fraction of its span per sub-block.
    const auto mainFilterQ = computeMainFilterQ (dynamicGainDbAbsSmoothed);
    lastAppliedFilterQ.store (mainFilterQ, std::memory_order_relaxed);

    if (! isExactlyUnchanged (mainFilterQ, cachedQ)
        || ! isExactlyUnchanged (gBase, cachedGBase)
        || svfType != cachedType)
    {
        coefficientCacheValid = false;
    }

    const auto kneeWidthDb = computeKneeWidthDb();
    const auto rangeMagnitudeDb = std::abs (rangeDb);
    const auto rangeSign = rangeDb > 0.0f ? 1.0f : -1.0f;
    const auto dynamicsActive = rangeDb != 0.0f;

    // True bypass for a band that cannot possibly change the signal: off, or
    // sitting at exactly 0 dB with no dynamics engaged. The SVF is already
    // bit-transparent at 0 dB (every gain-dependent mix scalar is exactly
    // zero - see TptSvf::makeCoefficients), so this is a CPU shortcut rather
    // than a correctness requirement, but it also keeps the "an idle band
    // does not touch its input at all" property the design brief's
    // guarantee #1 and tests/SaturationTests.cpp check for.
    const auto idle = ! dynamicsActive && staticGainDb == 0.0f && staticGainDbSmoothed == 0.0f;
    const auto filterActive = on && ! idle;

    const auto numMainChannels = juce::jmin (mainSubBlock.getNumChannels(), static_cast<size_t> (svf.getNumChannels()));

    detector.beginSubBlock();

    //==========================================================================
    // Per-sample gain path (SOTA brief F1): detector envelope -> dB -> gain
    // computer -> filter gain, every sample. There is no smoother anywhere
    // between the envelope and the filter; the envelope's own Attack/Release
    // ballistics are the only thing shaping how fast the band moves.

    float dynamicGainDb = 0.0f;

    for (size_t sample = 0; sample < numSamples; ++sample)
    {
        const auto levelDb = detector.processSample (detectorSource, startSample + sample);

        dynamicGainDb = 0.0f;

        if (dynamicsActive)
        {
            const auto overshootDb = levelDb - thresholdDb;
            const auto gainComputerDb = softKneeOvershoot (overshootDb, kneeWidthDb);
            const auto dynamicMagnitudeDb = juce::jlimit (0.0f, rangeMagnitudeDb, gainComputerDb);
            dynamicGainDb = dynamicMagnitudeDb * rangeSign;
        }

        // Static Gain only: a 15 ms one-pole so host automation of the knob
        // cannot step. Snapping at the end keeps 0 dB exactly reachable.
        staticGainDbSmoothed += (staticGainDb - staticGainDbSmoothed) * staticGainCoefficient;

        if (std::abs (staticGainDb - staticGainDbSmoothed) < smoothedGainSnapDb)
            staticGainDbSmoothed = staticGainDb;

        dynamicGainDbAbsSmoothed += (std::abs (dynamicGainDb) - dynamicGainDbAbsSmoothed) * gainQCouplingCoefficient;

        if (! filterActive)
            continue;

        const auto totalGainDb = staticGainDbSmoothed + dynamicGainDb;

        if (! coefficientCacheValid || ! isExactlyUnchanged (totalGainDb, cachedTotalGainDb))
        {
            cachedCoefficients = lnct::TptSvf::makeCoefficients (svfType, gBase, mainFilterQ, lnct::TptSvf::computeA (totalGainDb));
            cachedTotalGainDb = totalGainDb;
            cachedQ = mainFilterQ;
            cachedGBase = gBase;
            cachedType = svfType;
            coefficientCacheValid = true;
        }

        // Gentle, opt-in saturation (v0.3.0, docs/voicing-notes.md; ADAA1
        // kernel since v0.4.0) - only while this band is actively boosting,
        // never on a cut, matching the feature's "boosted bands" scope.
        const auto saturating = saturationEnabled && totalGainDb > 0.0f;
        const auto drive = saturating ? computeSaturationDrive (totalGainDb) : 0.0f;

        for (size_t channel = 0; channel < numMainChannels; ++channel)
        {
            auto* data = mainSubBlock.getChannelPointer (channel);

            auto value = svf.processSample (channel, data[sample], cachedCoefficients);

            // The ADAA kernel's previous-sample state is kept current even
            // while the stage is bypassed, so engaging saturation mid-stream
            // never starts from a difference quotient spanning a gap.
            if (saturating)
                value = saturator.process (channel, value, drive);
            else
                saturator.pushBypassed (channel, value);

            data[sample] = value;
        }
    }

    detector.endSubBlock (numSamples);

    // Telemetry (SOTA brief F7) - the last sample's dynamic gain, for the
    // future gain-reduction needle.
    lastAppliedDynamicGainDb.store (filterActive ? dynamicGainDb : 0.0f, std::memory_order_relaxed);
}

#include "Detector.h"
#include "RealtimeCoefficients.h"

#include <cmath>

void Detector::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate;

    const auto numChannels = static_cast<size_t> (spec.numChannels);

    // Built via emplace_back (matching juce::dsp::ProcessorDuplicator's own
    // internal construction, juce_ProcessorDuplicator.h) rather than
    // std::vector::assign() with a copied prototype, so every channel's
    // Filter is constructed directly from `bandpassCoefficients` without
    // relying on Filter's defaulted copy constructor to behave correctly
    // for an object that (before its own prepare() call below) may already
    // hold heap-allocated per-instance state.
    stage1.clear();
    stage2.clear();
    stage1.reserve (numChannels);
    stage2.reserve (numChannels);

    for (size_t channel = 0; channel < numChannels; ++channel)
    {
        stage1.emplace_back (bandpassCoefficients);
        stage2.emplace_back (bandpassCoefficients);
    }

    for (auto& filter : stage1)
        filter.prepare (spec);

    for (auto& filter : stage2)
        filter.prepare (spec);

    listenBuffer.setSize (static_cast<int> (numChannels), static_cast<int> (spec.maximumBlockSize));

    // Fixed fast-release coefficient for the auto-release fall-rate
    // reference envelope (see setAutoRelease()'s docs) - independent of
    // setReleaseMs(), recomputed only when sampleRate changes.
    fastReleaseCoefficient = static_cast<float> (std::exp (-1.0 / (sampleRate * (autoReleaseFloorMs * 0.001))));

    reset();
}

void Detector::reset()
{
    for (auto& filter : stage1)
        filter.reset();

    for (auto& filter : stage2)
        filter.reset();

    listenBuffer.clear();

    envelopeLinear = 0.0f;
    lastLevelDb = minusInfinityDb;

    outputEnvelopeLinear = 0.0f;
    autoReleaseCoefficient = releaseCoefficient;
    previousReferenceLevelDbForAutoRelease = minusInfinityDb;
    previousSubBlockNumSamplesForAutoRelease = 0;
    haveAutoReleaseReference = false;
    fastEnvelopeLinear = 0.0f;

    // A pending Wide->Split re-prime is satisfied by this full reset, so it
    // must not fire again on the next sub-block.
    bandpassNeedsReprime = false;
}

void Detector::setSplitMode (bool shouldBandpassTheDetectorInput) noexcept
{
    if (shouldBandpassTheDetectorInput == splitMode)
        return;

    splitMode = shouldBandpassTheDetectorInput;

    // Only the Wide -> Split direction needs the bandpass state cleared: it
    // has not been fed while Wide was engaged, so whatever is still sitting
    // in its delay elements belongs to whenever Split was last active and
    // would ring out as a stale transient. See setSplitMode()'s docs in
    // Detector.h.
    if (splitMode)
        bandpassNeedsReprime = true;
}

void Detector::setFrequencyAndQ (float frequencyHz, float q) noexcept
{
    const auto raw = juce::dsp::IIR::ArrayCoefficients<float>::makeBandPass (sampleRate, frequencyHz, q);
    lnct::applyBiquadCoefficients (*bandpassCoefficients, raw);
}

void Detector::setAttackMs (float attackMs) noexcept
{
    const auto attackSeconds = juce::jmax (0.00001, static_cast<double> (attackMs) * 0.001);
    attackCoefficient = static_cast<float> (std::exp (-1.0 / (sampleRate * attackSeconds)));
}

void Detector::setReleaseMs (float releaseMs) noexcept
{
    userReleaseMs = releaseMs;
    const auto releaseSeconds = juce::jmax (0.00001, static_cast<double> (releaseMs) * 0.001);
    releaseCoefficient = static_cast<float> (std::exp (-1.0 / (sampleRate * releaseSeconds)));
}

float Detector::processSubBlock (const juce::dsp::AudioBlock<const float>& preChainBlock,
                                  size_t startSample,
                                  size_t numSamples) noexcept
{
    if (numSamples == 0)
        return autoReleaseEnabled ? juce::Decibels::gainToDecibels (outputEnvelopeLinear, minusInfinityDb) : lastLevelDb;

    beginSubBlock();

    for (size_t sample = 0; sample < numSamples; ++sample)
        processSample (preChainBlock, startSample + sample);

    endSubBlock (numSamples);

    return lastLevelDb;
}

void Detector::beginSubBlock() noexcept
{
    if (bandpassNeedsReprime)
    {
        for (auto& filter : stage1)
            filter.reset();

        for (auto& filter : stage2)
            filter.reset();

        bandpassNeedsReprime = false;
    }

    // Auto-release coefficient derivation (see setAutoRelease()'s docs) -
    // once per sub-block, using ONLY the dedicated fast reference envelope
    // (`fastEnvelopeLinear`, fixed fast release, independent of the user's
    // own Release setting - see class docs for why the *slow* envelope
    // can't supply this measurement), so it genuinely reflects the true
    // signal's own recent behaviour rather than just measuring the user's
    // own release coefficient back at itself.
    if (autoReleaseEnabled)
    {
        const auto currentReferenceLevelDb = juce::Decibels::gainToDecibels (fastEnvelopeLinear, minusInfinityDb);

        if (haveAutoReleaseReference && previousSubBlockNumSamplesForAutoRelease > 0)
        {
            const auto elapsedSeconds = static_cast<float> (previousSubBlockNumSamplesForAutoRelease) / static_cast<float> (sampleRate);
            const auto fallDb = previousReferenceLevelDbForAutoRelease - currentReferenceLevelDb; // positive when falling
            const auto fallRateDbPerSecond = juce::jmax (0.0f, fallDb) / elapsedSeconds;

            if (fallRateDbPerSecond > 0.01f)
            {
                // Standard dB/neper relationship for a one-pole exponential
                // decay (20*log10(e) ~= 8.6859): the tau a pure exponential
                // falling at exactly this measured rate would have.
                constexpr float dbPerNeper = 8.6859f;
                const auto impliedTauSeconds = dbPerNeper / fallRateDbPerSecond;
                const auto impliedReleaseMs = impliedTauSeconds * 1000.0f;

                // min(user Release-ms, derived value) per the design brief -
                // never slower than the manual setting, never faster than
                // this plugin's own Release floor (stability).
                const auto effectiveReleaseMs = juce::jlimit (autoReleaseFloorMs, userReleaseMs, impliedReleaseMs);
                const auto effectiveReleaseSeconds = juce::jmax (0.00001f, effectiveReleaseMs * 0.001f);
                autoReleaseCoefficient = std::exp (-1.0f / (static_cast<float> (sampleRate) * effectiveReleaseSeconds));
            }
            else
            {
                // No measurable decay to derive a speed-up from (flat or
                // rising reference envelope) - fall back to the plain user
                // coefficient, which is exactly manual-Release behaviour.
                autoReleaseCoefficient = releaseCoefficient;
            }
        }
        else
        {
            autoReleaseCoefficient = releaseCoefficient;
        }

        previousReferenceLevelDbForAutoRelease = currentReferenceLevelDb;
        haveAutoReleaseReference = true;
    }

}

float Detector::processSample (const juce::dsp::AudioBlock<const float>& source, size_t sampleIndex) noexcept
{
    const auto numChannels = juce::jmin (source.getNumChannels(), stage1.size());

    float linkedAbs = 0.0f;

    for (size_t channel = 0; channel < numChannels; ++channel)
    {
        const auto input = source.getSample (static_cast<int> (channel), static_cast<int> (sampleIndex));

        // Split runs the cascaded bandpass so the detector only hears its
        // own band; Wide skips it entirely and follows the full-range
        // source (see setSplitMode()). Either way, whatever the envelope
        // follower actually hears is what lands in the listen buffer, so
        // the Listen solo always auditions the real detector feed.
        auto detectorInput = input;

        if (splitMode)
        {
            detectorInput = stage1[channel].processSample (input);
            detectorInput = stage2[channel].processSample (detectorInput);
        }

        listenBuffer.setSample (static_cast<int> (channel), static_cast<int> (sampleIndex), detectorInput);

        linkedAbs = juce::jmax (linkedAbs, std::abs (detectorInput));
    }

    // A non-finite source sample (upstream NaN/Inf) must not be able to
    // poison the envelope permanently - jmax above would propagate a NaN
    // straight into the recursion below, which never recovers on its own.
    if (! std::isfinite (linkedAbs))
        linkedAbs = 0.0f;

    // Reference envelope: always updated with the fixed user
    // attack/release coefficients, byte-for-byte identical to
    // v0.1.0's only envelope - this is what guarantee #1's
    // inertness/tolerant-import null test relies on when
    // autoReleaseEnabled is false, and what the auto-release
    // measurement above derives its "recent fall rate" from when true.
    if (linkedAbs > envelopeLinear)
        envelopeLinear = attackCoefficient * (envelopeLinear - linkedAbs) + linkedAbs;
    else
        envelopeLinear = releaseCoefficient * (envelopeLinear - linkedAbs) + linkedAbs;

    if (autoReleaseEnabled)
    {
        if (linkedAbs > outputEnvelopeLinear)
            outputEnvelopeLinear = attackCoefficient * (outputEnvelopeLinear - linkedAbs) + linkedAbs;
        else
            outputEnvelopeLinear = autoReleaseCoefficient * (outputEnvelopeLinear - linkedAbs) + linkedAbs;

        // Fast reference envelope (see class docs): same Attack, but
        // always the fixed fast release, independent of the user's own
        // Release-ms - this is what supplies the "recent fall rate"
        // measurement at the top of the next beginSubBlock() call.
        if (linkedAbs > fastEnvelopeLinear)
            fastEnvelopeLinear = attackCoefficient * (fastEnvelopeLinear - linkedAbs) + linkedAbs;
        else
            fastEnvelopeLinear = fastReleaseCoefficient * (fastEnvelopeLinear - linkedAbs) + linkedAbs;

        lastLevelDb = juce::Decibels::gainToDecibels (outputEnvelopeLinear, minusInfinityDb);
    }
    else
    {
        // Keep the output envelope mirrored/warm so re-enabling
        // autoRelease mid-stream never pops from a stale value.
        outputEnvelopeLinear = envelopeLinear;
        lastLevelDb = juce::Decibels::gainToDecibels (envelopeLinear, minusInfinityDb);
    }

    // The dB conversion is what v0.4.0 moved inside the sample loop: the
    // gain computer now runs per sample, so it needs the envelope in dB per
    // sample, not one settled value per sub-block (SOTA brief F1).
    return lastLevelDb;
}

void Detector::endSubBlock (size_t numSamples) noexcept
{
    if (autoReleaseEnabled)
        previousSubBlockNumSamplesForAutoRelease = numSamples;
}

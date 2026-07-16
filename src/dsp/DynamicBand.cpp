#include "DynamicBand.h"
#include "RealtimeCoefficients.h"

#include <cmath>

DynamicBand::DynamicBand (ShelfDirection shelfDirectionToUse) noexcept
    : shelfDirection (shelfDirectionToUse)
{
}

void DynamicBand::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate;

    mainFilter.prepare (spec);
    detector.prepare (spec);

    gainSmoothed.reset (sampleRate, smoothingTimeSeconds);
    gainSmoothed.setCurrentAndTargetValue (staticGainDb);

    reset();

    // Prime the detector's bandpass and the main filter's coefficients
    // immediately so the very first processSubBlock() call runs with
    // correct, non-default values.
    detector.setAttackMs (5.0f);
    detector.setReleaseMs (150.0f);
    detector.setFrequencyAndQ (frequencyHz, effectiveQ());
    updateFilterCoefficients (staticGainDb, effectiveQ());
}

void DynamicBand::reset()
{
    mainFilter.reset();
    detector.reset();
    gainSmoothed.setCurrentAndTargetValue (gainSmoothed.getTargetValue());
}

float DynamicBand::computeKneeWidthDb() const noexcept
{
    return juce::jlimit (kneeWidthFloorDb, kneeWidthCeilingDb, std::abs (rangeDb) * kneeWidthRangeSlope);
}

float DynamicBand::computeMainFilterQ (float dynamicGainDbAbs) const noexcept
{
    const auto baseQ = effectiveQ();

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

void DynamicBand::updateFilterCoefficients (float appliedGainDb, float mainFilterQ) noexcept
{
    const auto gainFactor = juce::Decibels::decibelsToGain (appliedGainDb);

    std::array<float, 6> raw {};

    if (! isEffectivelyShelf())
    {
        raw = juce::dsp::IIR::ArrayCoefficients<float>::makePeakFilter (sampleRate, frequencyHz, mainFilterQ, gainFactor);
    }
    else if (shelfDirection == ShelfDirection::low)
    {
        raw = juce::dsp::IIR::ArrayCoefficients<float>::makeLowShelf (sampleRate, frequencyHz, mainFilterQ, gainFactor);
    }
    else
    {
        raw = juce::dsp::IIR::ArrayCoefficients<float>::makeHighShelf (sampleRate, frequencyHz, mainFilterQ, gainFactor);
    }

    lnct::applyBiquadCoefficients (*mainFilter.state, raw);
}

void DynamicBand::processSubBlock (juce::dsp::AudioBlock<float> mainSubBlock,
                                    const juce::dsp::AudioBlock<const float>& preChainBlock,
                                    size_t startSample,
                                    size_t numSamples) noexcept
{
    if (numSamples == 0)
        return;

    // Detector frequency/Q are re-derived every sub-block from this band's
    // current parameters (cheap: one ArrayCoefficients::makeBandPass call),
    // matching the main filter's own once-per-sub-block coefficient
    // recompute below.
    detector.setFrequencyAndQ (frequencyHz, effectiveQ());

    // Always run the detector, even when this band is off or Range == 0,
    // so its envelope stays warm (no pop/jump if the band is re-enabled or
    // Range is brought back up from 0 mid-automation) and its listen
    // buffer (used by the Listen/solo feature) stays populated regardless
    // of the band's own on/off state.
    const auto levelDb = detector.processSubBlock (preChainBlock, startSample, numSamples);

    float dynamicGainDb = 0.0f;

    if (rangeDb != 0.0f)
    {
        const auto overshootDb = levelDb - thresholdDb;
        const auto gainComputerDb = softKneeOvershoot (overshootDb, computeKneeWidthDb());
        const auto dynamicMagnitudeDb = juce::jlimit (0.0f, std::abs (rangeDb), gainComputerDb);
        dynamicGainDb = dynamicMagnitudeDb * (rangeDb > 0.0f ? 1.0f : -1.0f);
    }

    const auto totalGainDb = staticGainDb + dynamicGainDb;
    gainSmoothed.setTargetValue (totalGainDb);
    const auto appliedGainDb = gainSmoothed.skip (static_cast<int> (numSamples));

    // Gain/Q coupling (v0.2.0, opt-in): the main filter's Q is derived from
    // the *instantaneous* dynamic gain magnitude (not the smoothed applied
    // gain) so it tracks the gain computer's own current decision - see
    // class comment.
    const auto mainFilterQ = computeMainFilterQ (std::abs (dynamicGainDb));

    // Coefficients are recomputed every sub-block regardless of `on`, so
    // toggling the band back on mid-stream never has to catch up from a
    // stale/default filter shape.
    updateFilterCoefficients (appliedGainDb, mainFilterQ);

    // appliedGainDb == 0.0f exactly (not just "close to 0") is a real,
    // reachable case - a static band (Range == 0) with Gain == 0 settles
    // there immediately (see above: gainSmoothed's target and current both
    // become bit-exact 0.0f, so skip() returns 0.0f exactly, not an
    // approximation). Mathematically, updateFilterCoefficients(0.0f)'s
    // resulting biquad *is* an exact identity filter (RBJ peaking/shelf
    // gain A == 1 makes the normalised b0 coefficient exactly 1.0 and the
    // {b1,b2} pair exactly equal the {a1,a2} pair bit-for-bit - see
    // RealtimeCoefficients.h). In practice, though, Transposed Direct
    // Form II's per-sample recursion does not preserve that exact
    // cancellation once the compiler contracts multiply+add pairs into
    // fused-multiply-add instructions (common on arm64/AVX2 with
    // -ffp-contract=on, JUCE's default build settings): measured deviation
    // for a single band at 0 dB gain was a real (not just floor-clamped)
    // ~-100 dB, compounding to ~-90 dB across all six bands in series -
    // short of guarantee #1's -120 dBFS bar (docs/design-brief.md). Skipping
    // `process()` entirely at exactly 0 dB - the same true-bypass path an
    // off band takes below - sidesteps that compiler-dependent FMA
    // contraction question entirely rather than fighting it, and is safe:
    // the filter's own state was already tracking within that same ~1e-7
    // relative error of an identity pass-through, so freezing it for the
    // (typically brief, and inaudible either way) duration spent at exactly
    // 0 dB does not introduce any perceptible discontinuity when gain next
    // moves away from 0.
    if (on && appliedGainDb != 0.0f)
    {
        juce::dsp::ProcessContextReplacing<float> context (mainSubBlock);
        mainFilter.process (context);
    }
    // else: true bypass - `mainSubBlock` is left untouched, which is what
    // guarantee #1 (bit-transparent null test) in docs/design-brief.md
    // relies on for an off band, or an on band settled at exactly 0 dB.
}

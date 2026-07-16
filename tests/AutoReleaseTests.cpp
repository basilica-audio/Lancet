#include "dsp/LancetEngine.h"
#include "TestHelpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

// Guarantee #3 (docs/design-brief.md §4): "Program-dependent release proof"
// - explicitly called out in the brief as "the single most important new
// test, proving the auto-release toggle actually changes gain-computer
// *behaviour*, not just that a boolean flag exists." See Detector.h's
// setAutoRelease() docs for the concrete mechanism this test exercises.
namespace
{
    constexpr double testSampleRate = 48000.0;
    constexpr double centreFrequencyHz = 1000.0;
    constexpr float thresholdDb = -20.0f;
    constexpr float rangeDb = -12.0f;
    constexpr float attackMs = 2.0f;
    constexpr float manualReleaseMs = 500.0f; // deliberately slow/musical, so any speed-up is unmistakable

    constexpr int holdSamples = static_cast<int> (0.3 * testSampleRate);      // above-threshold hold, lets Attack fully engage
    constexpr int decayTailSamples = static_cast<int> (1.2 * testSampleRate); // room for the slowest (manual) release to fully settle

    // Both scenarios below are genuinely *decaying* signals (never an
    // instantaneous full-scale step) - deliberately, so the comparison
    // isolates "how much natural decay information is in the signal" as
    // the only variable. An earlier version of this test used an abrupt
    // step-to-silence for the "sustained" case; that actually registered a
    // *larger* instantaneous fall-rate on the fast reference envelope than
    // a smooth decay does (a bigger, more sudden magnitude change), so it
    // settled *faster* under auto-release than the "transient" case did -
    // the opposite of this guarantee's intent, and a reminder that
    // auto-release's own mechanism (Detector.h's class docs) responds to
    // *how fast the signal is observed falling*, not to some abstract
    // "transient-ness" label.
    constexpr float fastDecayTauMs = 15.0f;  // "transient": a short, percussive-style natural decay
    constexpr float slowDecayTauMs = 200.0f; // "sustained": a longer, sustained-note-style natural decay

    constexpr float peakDbfs = -6.0f;
    constexpr float belowFloorDbfs = -70.0f;

    constexpr float settleToleranceDb = 0.5f;
    constexpr int windowSamples = static_cast<int> (0.005 * testSampleRate); // 5 ms measurement window
    constexpr int stepSamples = windowSamples;

    void prepareEngine (LancetEngine& engine, bool autoRelease, int maximumSamples)
    {
        // Band 3 (index 2) - always Bell, matching the rest of the suite's
        // gain-computer tests (DynamicBehaviorTests.cpp, KneeWidthTests.cpp).
        engine.setBandOn (2, true);
        engine.setBandFrequencyHz (2, static_cast<float> (centreFrequencyHz));
        engine.setBandQ (2, 1.0f);
        engine.setBandGainDb (2, 0.0f);
        engine.setBandRangeDb (2, rangeDb);
        engine.setBandThresholdDb (2, thresholdDb);
        engine.setBandAttackMs (2, attackMs);
        engine.setBandReleaseMs (2, manualReleaseMs);
        engine.setBandAutoRelease (2, autoRelease);

        engine.setInputTrimDb (0.0f);
        engine.setOutputTrimDb (0.0f);
        engine.setMixPercent (100.0f);

        juce::dsp::ProcessSpec spec;
        spec.sampleRate = testSampleRate;
        spec.maximumBlockSize = static_cast<juce::uint32> (maximumSamples);
        spec.numChannels = 1;
        engine.prepare (spec);
    }

    // A mono buffer: `holdSamples` at a steady `peakDbfs` sine (well above
    // Threshold, long enough for Attack to fully engage), followed by
    // `decayTailSamples` that decay the sine's own amplitude exponentially
    // at `decayTauMs` down toward silence - still audibly ringing as it
    // crosses back under Threshold. Phase-continuous throughout.
    juce::AudioBuffer<float> buildTestSignal (float decayTauMs)
    {
        juce::AudioBuffer<float> buffer (1, holdSamples + decayTailSamples);
        auto* data = buffer.getWritePointer (0);

        const auto peakAmplitude = 0.5f * juce::Decibels::decibelsToGain (peakDbfs);
        const auto floorAmplitude = 0.5f * juce::Decibels::decibelsToGain (belowFloorDbfs);
        const auto decayTauSamples = static_cast<double> (decayTauMs) * 0.001 * testSampleRate;

        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            float amplitude;

            if (i < holdSamples)
            {
                amplitude = peakAmplitude;
            }
            else
            {
                const auto tailIndex = static_cast<double> (i - holdSamples);
                amplitude = peakAmplitude * static_cast<float> (std::exp (-tailIndex / decayTauSamples));
                amplitude = juce::jmax (amplitude, floorAmplitude);
            }

            const auto phase = juce::MathConstants<double>::twoPi * centreFrequencyHz * static_cast<double> (i) / testSampleRate;
            data[i] = amplitude * static_cast<float> (std::sin (phase));
        }

        return buffer;
    }

    double rmsOfWindow (const juce::AudioBuffer<float>& buffer, int start, int length)
    {
        double sumOfSquares = 0.0;
        const auto* data = buffer.getReadPointer (0);
        const auto end = juce::jmin (buffer.getNumSamples(), start + length);

        for (int i = start; i < end; ++i)
            sumOfSquares += static_cast<double> (data[i]) * static_cast<double> (data[i]);

        const auto counted = end - start;
        return counted > 0 ? std::sqrt (sumOfSquares / static_cast<double> (counted)) : 0.0;
    }

    // Measures gain-vs-time-since-crossing by stepping a short window
    // through the decay tail and comparing each window's RMS in the
    // (already-known) dry input envelope against the same window's RMS in
    // the processed output - returns the sample offset (from the start of
    // the decay tail, i.e. `holdSamples`) at which the measured gain
    // magnitude first settles within `settleToleranceDb` of 0 dB (static
    // gain, since this band's static Gain is 0) and never departs from it
    // again for the remainder of the buffer.
    int measureSettleTimeSamples (bool autoRelease, float decayTauMs)
    {
        const auto dryBuffer = buildTestSignal (decayTauMs);

        juce::AudioBuffer<float> processed;
        processed.makeCopyOf (dryBuffer);

        LancetEngine engine;
        prepareEngine (engine, autoRelease, dryBuffer.getNumSamples());

        juce::dsp::AudioBlock<float> block (processed);
        engine.process (block);

        REQUIRE (TestHelpers::allSamplesFinite (processed));

        std::vector<double> gainDbAtStep;

        for (int start = holdSamples; start + windowSamples <= dryBuffer.getNumSamples(); start += stepSamples)
        {
            const auto inRms = rmsOfWindow (dryBuffer, start, windowSamples);
            const auto outRms = rmsOfWindow (processed, start, windowSamples);

            // Once the dry signal itself has decayed into the noise floor,
            // the RMS ratio is no longer a meaningful gain measurement -
            // stop before that point (both scenarios' floor is deep enough
            // below Threshold that gain has long since settled by then, per
            // the assertions below).
            if (inRms < 1.0e-6)
                break;

            gainDbAtStep.push_back (juce::Decibels::gainToDecibels (outRms / inRms, -300.0));
        }

        REQUIRE (! gainDbAtStep.empty());

        // First step at which the gain is within tolerance AND stays within
        // tolerance for the rest of the measured run (a single noisy dip
        // through the tolerance band shouldn't count as "settled").
        for (size_t i = 0; i < gainDbAtStep.size(); ++i)
        {
            const auto settledFromHere = std::all_of (gainDbAtStep.begin() + static_cast<long> (i), gainDbAtStep.end(),
                                                        [] (double g) { return std::abs (g) <= settleToleranceDb; });

            if (settledFromHere)
                return static_cast<int> (i) * stepSamples;
        }

        return static_cast<int> (gainDbAtStep.size()) * stepSamples; // never settled within the measured window
    }
}

TEST_CASE ("Auto-release: a fast-decaying transient settles back to static gain faster than a slower-decaying "
           "sustained note, and neither is ever slower than the plain manual-Release case",
           "[dsp][auto-release]")
{
    const auto transientSettleAuto = measureSettleTimeSamples (true, fastDecayTauMs);
    const auto transientSettleManual = measureSettleTimeSamples (false, fastDecayTauMs);
    const auto sustainedSettleAuto = measureSettleTimeSamples (true, slowDecayTauMs);
    const auto sustainedSettleManual = measureSettleTimeSamples (false, slowDecayTauMs);

    INFO ("transientSettleAuto (samples) = " << transientSettleAuto);
    INFO ("transientSettleManual (samples) = " << transientSettleManual);
    INFO ("sustainedSettleAuto (samples) = " << sustainedSettleAuto);
    INFO ("sustainedSettleManual (samples) = " << sustainedSettleManual);

    // Never *slower* than the manual Release setting - matching F6 ARC's
    // documented "always... shorter than the Release setting" promise.
    CHECK (transientSettleAuto <= transientSettleManual);
    CHECK (sustainedSettleAuto <= sustainedSettleManual);

    // The core proof: with auto-release on, the faster-decaying (15 ms tau)
    // transient settles measurably faster than the slower-decaying (200 ms
    // tau) sustained note - not just "not slower," a real, substantial
    // difference tracking the signal's own decay rate, proving the toggle
    // changes actual gain-computer behaviour rather than applying a single
    // fixed speed-up regardless of program material.
    CHECK (transientSettleAuto < sustainedSettleAuto);

    // The transient case's speed-up over its own manual baseline must be
    // substantial (not a rounding-error-sized difference) - this plugin's
    // concrete auto-release mechanism derives its effective release from
    // the fast reference envelope's own measured fall rate, which for this
    // fast-decaying (15 ms tau) transient should converge close to that
    // tau, dramatically faster than the 500 ms manual Release used here.
    CHECK (transientSettleAuto < transientSettleManual / 2);
}

TEST_CASE ("Auto-release toggle off is far less sensitive to the signal's own decay rate than toggle on is "
           "(sanity check)",
           "[dsp][auto-release]")
{
    // With auto-release off, the OUTPUT envelope's ballistics are governed
    // purely by the fixed 500 ms manual Release coefficient - it is not
    // perfectly rate-invariant (a slow envelope still lags a slower-moving
    // target for somewhat longer than a faster-moving one), but that
    // residual sensitivity should be small relative to the deliberate,
    // large, decay-rate-tracking difference auto-release produces on
    // purpose (the main TEST_CASE above: samples in the thousands, not a
    // rounding-error difference). This is a sanity check that the "off"
    // path's small residual sensitivity doesn't itself already explain the
    // "on" path's own, much larger, decay-rate-tracking behaviour.
    const auto transientSettleAuto = measureSettleTimeSamples (true, fastDecayTauMs);
    const auto sustainedSettleAuto = measureSettleTimeSamples (true, slowDecayTauMs);
    const auto transientSettleManual = measureSettleTimeSamples (false, fastDecayTauMs);
    const auto sustainedSettleManual = measureSettleTimeSamples (false, slowDecayTauMs);

    INFO ("transientSettleAuto (samples) = " << transientSettleAuto);
    INFO ("sustainedSettleAuto (samples) = " << sustainedSettleAuto);
    INFO ("transientSettleManual (samples) = " << transientSettleManual);
    INFO ("sustainedSettleManual (samples) = " << sustainedSettleManual);

    const auto autoSpreadSamples = sustainedSettleAuto - transientSettleAuto;
    const auto manualSpreadSamples = sustainedSettleManual - transientSettleManual;

    REQUIRE (autoSpreadSamples > 0);
    CHECK (manualSpreadSamples < autoSpreadSamples);
}

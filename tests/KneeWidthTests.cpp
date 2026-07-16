#include "dsp/LancetEngine.h"
#include "TestHelpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

// Guarantee #2 (docs/design-brief.md §4): "Knee-width curve proof" - for a
// fixed (freq, Q, Threshold) triple, sweep Range across at least three
// values and measure the actual gain-vs-overshoot curve around Threshold,
// assert the measured knee width scales with Range per
// clamp(|range|*0.5, 2, 10), and that the +-12 dB case matches v0.1.0's old
// fixed-6-dB-knee shape - a real measured-curve proof, not just "the
// formula compiles."
//
// Measurement technique: hold a steady tone at a fixed dB overshoot above
// Threshold long enough for the detector's own envelope, its downstream
// gain computer, AND DynamicBand's separate 50 ms gain-smoothing ramp (an
// anti-zipper SmoothedValue, unrelated to the user's Attack/Release) to all
// fully settle, then measure the steady-state gain via the same tail-RMS-
// ratio technique tests/DynamicBehaviorTests.cpp already uses. Deliberately
// reuses that same test's own proven-stable Attack/Release pair (2 ms/
// 100 ms) rather than more extreme values: a very fast Attack/Release
// (originally tried here) keeps the envelope's own steady-state level
// chattering at audio-rate around a sine tone's rectified ripple, which
// continually re-triggers the 50 ms gain-smoother's ramp countdown
// (juce::SmoothedValue::setTargetValue() restarts its ramp whenever the
// target actually changes) and prevents it from ever fully converging -
// slower, audio-period-insensitive ballistics avoid that pitfall entirely.
// At the knee's own centre (0 dB overshoot, i.e. exactly at Threshold), the
// quadratic soft-knee formula (DynamicBand.cpp's softKneeOvershoot())
// evaluates to exactly kneeWidth/8 - directly proportional to the knee
// width itself - which is what lets this test derive a "measured knee
// width" number from a single clean sample point rather than fuzzy
// edge-detection across a scan.
namespace
{
    constexpr double testSampleRate = 48000.0;
    constexpr double centreFrequencyHz = 1000.0;
    constexpr float thresholdDb = -30.0f;

    constexpr float attackMs = 2.0f;
    constexpr float releaseMs = 100.0f;

    // >= 5x Release plus margin, clearing both the detector's own envelope
    // settling and DynamicBand's separate 50 ms gain-smoothing ramp - see
    // class comment.
    constexpr double holdSeconds = 0.7;
    constexpr int holdSamples = static_cast<int> (holdSeconds * testSampleRate);
    constexpr int measureWindowSamples = static_cast<int> (0.05 * testSampleRate);

    void prepareEngine (LancetEngine& engine, float rangeDb, int maximumSamples)
    {
        // Band 3 (index 2) - always Bell, isolating the gain-computer logic
        // from shelf-mode filter-shape concerns, matching
        // DynamicBehaviorTests.cpp's own convention.
        engine.setBandOn (2, true);
        engine.setBandFrequencyHz (2, static_cast<float> (centreFrequencyHz));
        engine.setBandQ (2, 1.0f);
        engine.setBandGainDb (2, 0.0f);
        engine.setBandRangeDb (2, rangeDb);
        engine.setBandThresholdDb (2, thresholdDb);
        engine.setBandAttackMs (2, attackMs);
        engine.setBandReleaseMs (2, releaseMs);

        engine.setInputTrimDb (0.0f);
        engine.setOutputTrimDb (0.0f);
        engine.setMixPercent (100.0f);

        juce::dsp::ProcessSpec spec;
        spec.sampleRate = testSampleRate;
        spec.maximumBlockSize = static_cast<juce::uint32> (maximumSamples);
        spec.numChannels = 1;
        engine.prepare (spec);
    }

    // The cascaded 2x bandpass + peak-follower Detector (Detector.h's class
    // comment; see also tests/DetectorTests.cpp's own ">-3 dB" tolerance on
    // "settles near full level") reads a small, consistent amount below a
    // tone's true input dBFS even exactly at its own centre frequency -
    // real, expected peak-detector insertion loss, not a bug. This
    // measurement's whole point is comparing the detector's *own* reading
    // against the band's absolute Threshold parameter, so that fixed offset
    // must be calibrated out first (measured once, from a throwaway
    // Range=0 - i.e. no dynamic-gain feedback complicating the raw
    // reading - band at a known input level), otherwise it would silently
    // skew every overshoot this file requests by however many dB the
    // detector itself under-reads by.
    float calibrateDetectorOffsetDb()
    {
        constexpr float knownInputDb = -20.0f;

        LancetEngine engine;
        prepareEngine (engine, 0.0f, holdSamples); // Range=0: static band, detector still runs, no gain feedback

        juce::AudioBuffer<float> buffer (1, holdSamples);
        TestHelpers::fillWithSine (buffer, testSampleRate, centreFrequencyHz, juce::Decibels::decibelsToGain (knownInputDb));

        juce::dsp::AudioBlock<float> block (buffer);
        engine.process (block);

        return engine.getBand (2).getLastDetectorLevelDb() - knownInputDb;
    }

    // Measures steady-state gain (dB) at a given overshoot-above-threshold
    // (dB), continuing the SAME engine/phase across calls so the sine stays
    // phase-continuous hold-to-hold (matching TestHelpers::fillWithSine's
    // own phase-continuity contract). `detectorOffsetDb` is
    // calibrateDetectorOffsetDb()'s own correction (see its docs) - applied
    // here so the DETECTOR itself reads `thresholdDb + overshootDb`
    // exactly, not just the raw input signal.
    double measureSettledGainDb (LancetEngine& engine, double& phaseSamples, float overshootDb, float detectorOffsetDb)
    {
        const auto amplitude = juce::Decibels::decibelsToGain (thresholdDb + overshootDb - detectorOffsetDb);

        juce::AudioBuffer<float> reference (1, holdSamples);
        TestHelpers::fillWithSine (reference, testSampleRate, centreFrequencyHz, amplitude, static_cast<juce::int64> (phaseSamples));
        phaseSamples += holdSamples;

        juce::AudioBuffer<float> processed;
        processed.makeCopyOf (reference);

        juce::dsp::AudioBlock<float> block (processed);
        engine.process (block);

        REQUIRE (TestHelpers::allSamplesFinite (processed));

        const auto start = holdSamples - measureWindowSamples;
        const auto inRms = TestHelpers::tailRms (reference, start);
        const auto outRms = TestHelpers::tailRms (processed, start);
        REQUIRE (inRms > 0.0);
        return juce::Decibels::gainToDecibels (outRms / inRms);
    }
}

TEST_CASE ("Knee-width curve: measured gain-vs-overshoot shape scales with Range per clamp(|range|*0.5, 2, 10)",
           "[dsp][knee]")
{
    static constexpr float rangesToTest[] = { -2.0f, -6.0f, -12.0f };
    const auto detectorOffsetDb = calibrateDetectorOffsetDb();
    INFO ("calibrated detector offset = " << detectorOffsetDb << " dB");

    for (const auto rangeDb : rangesToTest)
    {
        INFO ("Range = " << rangeDb << " dB");

        const auto expectedKneeWidthDb = juce::jlimit (2.0f, 10.0f, std::abs (rangeDb) * 0.5f);

        LancetEngine engine;
        prepareEngine (engine, rangeDb, holdSamples);
        double phaseSamples = 0.0;

        // Deep below the knee (well past the lower edge at -kneeWidth/2):
        // zero dynamic gain.
        const auto deepBelowGainDb = measureSettledGainDb (engine, phaseSamples, -expectedKneeWidthDb * 2.0f, detectorOffsetDb);
        CHECK (deepBelowGainDb == Catch::Approx (0.0).margin (0.15));

        // Exactly at the knee's own centre (Threshold, 0 dB overshoot): the
        // quadratic soft-knee formula gives gc(0) = kneeWidth/8 exactly -
        // this single clean point directly pins down the knee's WIDTH (not
        // just its presence), since it scales linearly with it.
        const auto centreGainDb = measureSettledGainDb (engine, phaseSamples, 0.0f, detectorOffsetDb);
        const auto measuredKneeWidthDb = std::abs (centreGainDb) * 8.0;
        CHECK (measuredKneeWidthDb == Catch::Approx (static_cast<double> (expectedKneeWidthDb)).margin (0.3));

        // Deep above the knee (well past the upper edge at +kneeWidth/2):
        // fully saturated at |Range|.
        const auto deepAboveGainDb = measureSettledGainDb (engine, phaseSamples, expectedKneeWidthDb * 2.0f, detectorOffsetDb);
        CHECK (deepAboveGainDb == Catch::Approx (static_cast<double> (rangeDb)).margin (0.3));

        // The knee is genuinely soft (graduated), not a hard switch: the
        // magnitude at the centre must sit strictly between "off" and
        // "fully saturated."
        CHECK (std::abs (centreGainDb) > 0.05);
        CHECK (std::abs (centreGainDb) < std::abs (rangeDb) - 0.05);
    }
}

TEST_CASE ("Knee width at Range = +-12 dB measures ~6 dB, matching v0.1.0's fixed-knee shape", "[dsp][knee]")
{
    // v0.1.0 used a flat 6 dB knee constant unconditionally; the new
    // Range-derived formula reproduces exactly that value at Range = +-12
    // dB (12 * 0.5 = 6, within the [2, 10] clamp) - see docs/design-brief.md
    // §3's honesty note on this being a deliberate bit-compatible-in-shape
    // special case.
    const auto detectorOffsetDb = calibrateDetectorOffsetDb();

    LancetEngine engine;
    prepareEngine (engine, -12.0f, holdSamples);
    double phaseSamples = 0.0;

    const auto centreGainDb = measureSettledGainDb (engine, phaseSamples, 0.0f, detectorOffsetDb);
    const auto measuredKneeWidthDb = std::abs (centreGainDb) * 8.0;

    CHECK (measuredKneeWidthDb == Catch::Approx (6.0).margin (0.3));
}

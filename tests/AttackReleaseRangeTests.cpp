#include "dsp/Detector.h"
#include "TestHelpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>

// Guarantee #5 (docs/design-brief.md §4): "Attack/Release range-boundary
// tests" - Attack = 0.1 ms and 500 ms (the new v0.2.0 floor/ceiling) both
// produce finite, correctly-clamped coefficient updates (no NaN/Inf, no
// assertion failure) and measurably different envelope-follower time
// constants from each other and from the v0.1.0 boundary values they
// replace (0.5 ms/100 ms); same pattern for Release at 5 ms/1500 ms
// (replacing 10 ms/1000 ms).
//
// Because Attack/Release now span a very wide ratio (0.1-500 ms is a 5000x
// range; 5-1500 ms is 300x), no single elapsed-time snapshot can
// distinguish every adjacent pair at once (a snapshot short enough to still
// show the fastest setting mid-transition leaves the slowest settings
// statistically indistinguishable from their starting point, and vice
// versa) - each adjacent pair below is therefore measured at its own
// tailored elapsed time, chosen to sit within roughly one time constant of
// the faster setting in that pair.
namespace
{
    constexpr double testSampleRate = 48000.0;
    constexpr double toneHz = 1000.0;
    constexpr int settlePhaseSamples = 8192;
    constexpr float fullAmplitude = 0.8f;

    juce::dsp::ProcessSpec makeSpec (int maxSamples)
    {
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = testSampleRate;
        spec.maximumBlockSize = static_cast<juce::uint32> (maxSamples);
        spec.numChannels = 1;
        return spec;
    }

    // Measures the envelope level (dBFS) `elapsedSamples` after an abrupt
    // step from silence to a full-level in-band tone, with Attack set to
    // `attackMs` (Release fixed deliberately slow/irrelevant to this
    // attack-phase measurement).
    float measureAttackProgressDb (float attackMs, int elapsedSamples)
    {
        Detector detector;
        detector.prepare (makeSpec (juce::jmax (settlePhaseSamples, elapsedSamples)));
        detector.setFrequencyAndQ (static_cast<float> (toneHz), 1.0f);
        detector.setAttackMs (attackMs);
        detector.setReleaseMs (2000.0f);

        juce::AudioBuffer<float> silence (1, settlePhaseSamples);
        silence.clear();
        const juce::dsp::AudioBlock<const float> silentBlock (silence);
        detector.processSubBlock (silentBlock, 0, static_cast<size_t> (settlePhaseSamples));

        juce::AudioBuffer<float> stepBuffer (1, elapsedSamples);
        TestHelpers::fillWithSine (stepBuffer, testSampleRate, toneHz, fullAmplitude, settlePhaseSamples);
        const juce::dsp::AudioBlock<const float> stepBlock (stepBuffer);

        return detector.processSubBlock (stepBlock, 0, static_cast<size_t> (elapsedSamples));
    }

    // Mirrored for the release phase: settle at full level (fast, fixed
    // Attack, irrelevant to this measurement), then step down to silence,
    // and measure `elapsedSamples` later.
    float measureReleaseProgressDb (float releaseMs, int elapsedSamples)
    {
        Detector detector;
        detector.prepare (makeSpec (juce::jmax (settlePhaseSamples, elapsedSamples)));
        detector.setFrequencyAndQ (static_cast<float> (toneHz), 1.0f);
        detector.setAttackMs (0.1f);
        detector.setReleaseMs (releaseMs);

        juce::AudioBuffer<float> loud (1, settlePhaseSamples);
        TestHelpers::fillWithSine (loud, testSampleRate, toneHz, fullAmplitude);
        const juce::dsp::AudioBlock<const float> loudBlock (loud);
        detector.processSubBlock (loudBlock, 0, static_cast<size_t> (settlePhaseSamples));

        juce::AudioBuffer<float> silence (1, elapsedSamples);
        silence.clear();
        const juce::dsp::AudioBlock<const float> silentBlock (silence);

        return detector.processSubBlock (silentBlock, 0, static_cast<size_t> (elapsedSamples));
    }

    // Basic finite-output/no-crash robustness sweep at a given Attack or
    // Release boundary value, across a long, denormal-adjacent run.
    void checkFiniteOverLongRun (float attackMs, float releaseMs)
    {
        Detector detector;
        constexpr int longRunSamples = 48000 * 2;
        detector.prepare (makeSpec (longRunSamples));
        detector.setFrequencyAndQ (static_cast<float> (toneHz), 1.0f);
        detector.setAttackMs (attackMs);
        detector.setReleaseMs (releaseMs);

        juce::AudioBuffer<float> buffer (1, longRunSamples);

        for (int i = 0; i < longRunSamples; ++i)
        {
            // Alternates loud/near-silent every 4800 samples (100 ms) to
            // repeatedly exercise both the attack and release branch.
            const auto amplitude = ((i / 4800) % 2 == 0) ? fullAmplitude : 0.0f;
            const auto phase = juce::MathConstants<double>::twoPi * toneHz * static_cast<double> (i) / testSampleRate;
            buffer.setSample (0, i, amplitude * static_cast<float> (std::sin (phase)));
        }

        const juce::dsp::AudioBlock<const float> block (buffer);
        float levelDb = 0.0f;
        CHECK_NOTHROW (levelDb = detector.processSubBlock (block, 0, static_cast<size_t> (longRunSamples)));

        CHECK (std::isfinite (levelDb));
        CHECK (TestHelpers::allSamplesFinite (detector.getListenBuffer()));
    }
}

TEST_CASE ("Attack range boundaries (0.1-500 ms) produce finite output at both new extremes", "[dsp][attack-release-range]")
{
    checkFiniteOverLongRun (0.1f, 150.0f);  // new floor
    checkFiniteOverLongRun (500.0f, 150.0f); // new ceiling
}

TEST_CASE ("Attack range boundaries: each adjacent pair (new floor/old floor/old ceiling/new ceiling) is "
           "measurably, correctly ordered - faster Attack settles more within the same elapsed window",
           "[dsp][attack-release-range]")
{
    // Pair 1: new floor (0.1 ms) vs old floor (0.5 ms), measured ~1 time
    // constant into the faster (0.1 ms) setting.
    {
        constexpr int elapsedSamples = static_cast<int> (0.0001 * testSampleRate); // 0.1 ms
        const auto newFloorDb = measureAttackProgressDb (0.1f, elapsedSamples);
        const auto oldFloorDb = measureAttackProgressDb (0.5f, elapsedSamples);

        INFO ("newFloor(0.1ms)=" << newFloorDb << " oldFloor(0.5ms)=" << oldFloorDb);
        CHECK (std::isfinite (newFloorDb));
        CHECK (std::isfinite (oldFloorDb));
        CHECK (newFloorDb > oldFloorDb);
        CHECK ((newFloorDb - oldFloorDb) > 3.0f);
    }

    // Pair 2: old floor (0.5 ms) vs old ceiling (100 ms), measured at 2 ms
    // (well past 0.5 ms's own settle, still early for 100 ms's).
    {
        constexpr int elapsedSamples = static_cast<int> (0.002 * testSampleRate); // 2 ms
        const auto oldFloorDb = measureAttackProgressDb (0.5f, elapsedSamples);
        const auto oldCeilingDb = measureAttackProgressDb (100.0f, elapsedSamples);

        INFO ("oldFloor(0.5ms)=" << oldFloorDb << " oldCeiling(100ms)=" << oldCeilingDb);
        CHECK (std::isfinite (oldFloorDb));
        CHECK (std::isfinite (oldCeilingDb));
        CHECK (oldFloorDb > oldCeilingDb);
        CHECK ((oldFloorDb - oldCeilingDb) > 3.0f);
    }

    // Pair 3: old ceiling (100 ms) vs new ceiling (500 ms), measured at
    // 150 ms (1.5 time constants into the faster (100 ms) setting).
    {
        constexpr int elapsedSamples = static_cast<int> (0.15 * testSampleRate); // 150 ms
        const auto oldCeilingDb = measureAttackProgressDb (100.0f, elapsedSamples);
        const auto newCeilingDb = measureAttackProgressDb (500.0f, elapsedSamples);

        INFO ("oldCeiling(100ms)=" << oldCeilingDb << " newCeiling(500ms)=" << newCeilingDb);
        CHECK (std::isfinite (oldCeilingDb));
        CHECK (std::isfinite (newCeilingDb));
        CHECK (oldCeilingDb > newCeilingDb);
        CHECK ((oldCeilingDb - newCeilingDb) > 3.0f);
    }
}

TEST_CASE ("Release range boundaries (5-1500 ms) produce finite output at both new extremes", "[dsp][attack-release-range]")
{
    checkFiniteOverLongRun (2.0f, 5.0f);    // new floor
    checkFiniteOverLongRun (2.0f, 1500.0f); // new ceiling
}

TEST_CASE ("Release range boundaries: each adjacent pair (new floor/old floor/old ceiling/new ceiling) is "
           "measurably, correctly ordered - faster Release decays further within the same elapsed window",
           "[dsp][attack-release-range]")
{
    // Pair 1: new floor (5 ms) vs old floor (10 ms), measured at 5 ms
    // (1 time constant into the faster setting).
    {
        constexpr int elapsedSamples = static_cast<int> (0.005 * testSampleRate); // 5 ms
        const auto newFloorDb = measureReleaseProgressDb (5.0f, elapsedSamples);
        const auto oldFloorDb = measureReleaseProgressDb (10.0f, elapsedSamples);

        INFO ("newFloor(5ms)=" << newFloorDb << " oldFloor(10ms)=" << oldFloorDb);
        CHECK (std::isfinite (newFloorDb));
        CHECK (std::isfinite (oldFloorDb));
        CHECK (newFloorDb < oldFloorDb);
        CHECK ((oldFloorDb - newFloorDb) > 2.0f);
    }

    // Pair 2: old floor (10 ms) vs old ceiling (1000 ms), measured at 50 ms.
    {
        constexpr int elapsedSamples = static_cast<int> (0.05 * testSampleRate); // 50 ms
        const auto oldFloorDb = measureReleaseProgressDb (10.0f, elapsedSamples);
        const auto oldCeilingDb = measureReleaseProgressDb (1000.0f, elapsedSamples);

        INFO ("oldFloor(10ms)=" << oldFloorDb << " oldCeiling(1000ms)=" << oldCeilingDb);
        CHECK (std::isfinite (oldFloorDb));
        CHECK (std::isfinite (oldCeilingDb));
        CHECK (oldFloorDb < oldCeilingDb);
        CHECK ((oldCeilingDb - oldFloorDb) > 2.0f);
    }

    // Pair 3: old ceiling (1000 ms) vs new ceiling (1500 ms), measured at
    // 800 ms.
    {
        constexpr int elapsedSamples = static_cast<int> (0.8 * testSampleRate); // 800 ms
        const auto oldCeilingDb = measureReleaseProgressDb (1000.0f, elapsedSamples);
        const auto newCeilingDb = measureReleaseProgressDb (1500.0f, elapsedSamples);

        INFO ("oldCeiling(1000ms)=" << oldCeilingDb << " newCeiling(1500ms)=" << newCeilingDb);
        CHECK (std::isfinite (oldCeilingDb));
        CHECK (std::isfinite (newCeilingDb));
        CHECK (oldCeilingDb < newCeilingDb);
        CHECK ((newCeilingDb - oldCeilingDb) > 1.0f);
    }
}

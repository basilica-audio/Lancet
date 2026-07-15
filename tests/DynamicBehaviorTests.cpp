#include "dsp/LancetEngine.h"
#include "TestHelpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

// Guarantee #3 (docs/design-brief.md): "sine at band centre, level stepped
// above threshold -> measured band gain approaches range (+-1 dB) after 5x
// release time; below threshold -> static."
namespace
{
    constexpr double testSampleRate = 48000.0;
    constexpr double centreFrequencyHz = 1000.0;
    constexpr float releaseMs = 100.0f;
    constexpr float attackMs = 2.0f;
    constexpr float thresholdDb = -30.0f;
    constexpr float rangeDb = -12.0f; // cut when loud

    // A window's worth of measured gain (dB): RMS ratio of `processed` to
    // `reference` over the last `windowSamples` samples of the buffer,
    // where both cover the same synthetic tone.
    double measureTailGainDb (const juce::AudioBuffer<float>& reference, const juce::AudioBuffer<float>& processed, int windowSamples)
    {
        const auto start = juce::jmax (0, reference.getNumSamples() - windowSamples);
        const auto inRms = TestHelpers::tailRms (reference, start);
        const auto outRms = TestHelpers::tailRms (processed, start);

        REQUIRE (inRms > 0.0);
        return juce::Decibels::gainToDecibels (outRms / inRms);
    }

    // LancetEngine is non-copyable/non-movable (JUCE_DECLARE_NON_COPYABLE_
    // WITH_LEAK_DETECTOR), so this configures an existing instance in place
    // rather than returning one by value.
    void prepareEngine (LancetEngine& engine, int maximumSamples)
    {
        // Band 3 (index 2) - always Bell, so this isolates the gain-
        // computer/ballistics logic from shelf-mode filter-shape concerns.
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
}

TEST_CASE ("Dynamic behaviour: below threshold, band gain stays static (near 0 dB)", "[dsp][dynamic]")
{
    // -40 dBFS sine at 0.5 linear amplitude scaling -> well below the -30
    // dB threshold (10 dB of headroom, outside the 6 dB soft knee), so the
    // gain computer should report exactly 0 dB of dynamic gain throughout.
    const auto belowThresholdAmplitude = 0.5f * juce::Decibels::decibelsToGain (-40.0f);

    constexpr int numSamples = static_cast<int> (0.4 * testSampleRate); // 400 ms - several attack/release times
    LancetEngine engine;
    prepareEngine (engine, numSamples);

    juce::AudioBuffer<float> reference (1, numSamples);
    TestHelpers::fillWithSine (reference, testSampleRate, centreFrequencyHz, belowThresholdAmplitude);

    juce::AudioBuffer<float> processed;
    processed.makeCopyOf (reference);

    juce::dsp::AudioBlock<float> block (processed);
    engine.process (block);

    const auto measuredGainDb = measureTailGainDb (reference, processed, static_cast<int> (0.05 * testSampleRate));
    CHECK (measuredGainDb == Catch::Approx (0.0).margin (1.0));
}

TEST_CASE ("Dynamic behaviour: level stepped above threshold converges to Range within +-1 dB after 5x release time",
           "[dsp][dynamic]")
{
    constexpr float belowThresholdDbfs = -40.0f;
    constexpr float aboveThresholdDbfs = -6.0f; // 24 dB above threshold - well past the 6 dB knee, saturates at |Range|

    const auto belowAmplitude = 0.5f * juce::Decibels::decibelsToGain (belowThresholdDbfs);
    const auto aboveAmplitude = 0.5f * juce::Decibels::decibelsToGain (aboveThresholdDbfs);

    constexpr int belowPhaseSamples = static_cast<int> (0.3 * testSampleRate);
    // 5x release time plus margin, per the guarantee's own convergence
    // criterion.
    constexpr int abovePhaseSamples = static_cast<int> (5.0 * (releaseMs / 1000.0) * testSampleRate) + static_cast<int> (0.1 * testSampleRate);

    LancetEngine engine;
    prepareEngine (engine, juce::jmax (belowPhaseSamples, abovePhaseSamples));

    // Phase 1: settle below threshold (static).
    juce::AudioBuffer<float> belowReference (1, belowPhaseSamples);
    TestHelpers::fillWithSine (belowReference, testSampleRate, centreFrequencyHz, belowAmplitude, 0);

    juce::AudioBuffer<float> belowProcessed;
    belowProcessed.makeCopyOf (belowReference);

    juce::dsp::AudioBlock<float> belowBlock (belowProcessed);
    engine.process (belowBlock);

    // Phase 2: step the level above threshold and hold for >= 5x release
    // time, continuing the same engine instance (and therefore the same
    // envelope/filter state) and the same sine's phase.
    juce::AudioBuffer<float> aboveReference (1, abovePhaseSamples);
    TestHelpers::fillWithSine (aboveReference, testSampleRate, centreFrequencyHz, aboveAmplitude, belowPhaseSamples);

    juce::AudioBuffer<float> aboveProcessed;
    aboveProcessed.makeCopyOf (aboveReference);

    juce::dsp::AudioBlock<float> aboveBlock (aboveProcessed);
    engine.process (aboveBlock);

    REQUIRE (TestHelpers::allSamplesFinite (aboveProcessed));

    const auto measuredGainDb = measureTailGainDb (aboveReference, aboveProcessed, static_cast<int> (0.05 * testSampleRate));
    CHECK (measuredGainDb == Catch::Approx (static_cast<double> (rangeDb)).margin (1.0));
}

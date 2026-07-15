#include "dsp/LancetEngine.h"
#include "TestHelpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

// General LancetEngine integration coverage: trims, Mix blend, and
// multi-band interaction, beyond the ten guarantee-specific test files.
namespace
{
    constexpr double testSampleRate = 48000.0;
    constexpr int testBlockSize = 8192;
    constexpr int settleSamples = 4096;

    juce::dsp::ProcessSpec makeTestSpec (int numChannels)
    {
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = testSampleRate;
        spec.maximumBlockSize = static_cast<juce::uint32> (testBlockSize);
        spec.numChannels = static_cast<juce::uint32> (numChannels);
        return spec;
    }
}

TEST_CASE ("Engine: Input Trim and Output Trim apply their full dB value with every band off", "[dsp][engine]")
{
    LancetEngine engine;
    engine.setInputTrimDb (6.0f);
    engine.setOutputTrimDb (3.0f);
    engine.prepare (makeTestSpec (1));

    juce::AudioBuffer<float> reference (1, testBlockSize);
    TestHelpers::fillWithSine (reference, testSampleRate, 1000.0, 0.3f);

    juce::AudioBuffer<float> processed;
    processed.makeCopyOf (reference);

    juce::dsp::AudioBlock<float> block (processed);
    engine.process (block);

    const auto inRms = TestHelpers::tailRms (reference, settleSamples);
    const auto outRms = TestHelpers::tailRms (processed, settleSamples);

    REQUIRE (inRms > 0.0);
    CHECK (juce::Decibels::gainToDecibels (outRms / inRms) == Catch::Approx (9.0).margin (0.1));
}

TEST_CASE ("Engine: Mix at 0% is a bypass of the band chain (still subject to trims)", "[dsp][engine][mix]")
{
    LancetEngine engine;
    engine.setBandOn (2, true);
    engine.setBandFrequencyHz (2, 1000.0f);
    engine.setBandGainDb (2, 12.0f);
    engine.setBandRangeDb (2, 0.0f);
    engine.setMixPercent (0.0f);

    engine.prepare (makeTestSpec (1));

    juce::AudioBuffer<float> reference (1, testBlockSize);
    TestHelpers::fillWithSine (reference, testSampleRate, 1000.0, 0.5f);

    juce::AudioBuffer<float> processed;
    processed.makeCopyOf (reference);

    juce::dsp::AudioBlock<float> block (processed);
    engine.process (block);

    const auto inRms = TestHelpers::tailRms (reference, settleSamples);
    const auto outRms = TestHelpers::tailRms (processed, settleSamples);

    REQUIRE (inRms > 0.0);
    // Even with a +12 dB bell engaged, Mix=0% should leave the signal
    // essentially untouched (dry only).
    CHECK (juce::Decibels::gainToDecibels (outRms / inRms) == Catch::Approx (0.0).margin (0.5));
}

TEST_CASE ("Engine: Mix at 100% is fully wet (the band's own effect reaches the output in full)", "[dsp][engine][mix]")
{
    LancetEngine engineWet;
    engineWet.setBandOn (2, true);
    engineWet.setBandFrequencyHz (2, 1000.0f);
    engineWet.setBandGainDb (2, -8.0f);
    engineWet.setBandRangeDb (2, 0.0f);
    engineWet.setMixPercent (100.0f);
    engineWet.prepare (makeTestSpec (1));

    juce::AudioBuffer<float> reference (1, testBlockSize);
    TestHelpers::fillWithSine (reference, testSampleRate, 1000.0, 0.5f);

    juce::AudioBuffer<float> processed;
    processed.makeCopyOf (reference);

    juce::dsp::AudioBlock<float> block (processed);
    engineWet.process (block);

    const auto inRms = TestHelpers::tailRms (reference, settleSamples);
    const auto outRms = TestHelpers::tailRms (processed, settleSamples);

    REQUIRE (inRms > 0.0);
    // -8 dB bell squarely on the probe tone should measurably cut it.
    CHECK (juce::Decibels::gainToDecibels (outRms / inRms) < -3.0);
}

TEST_CASE ("Engine: two bands at different frequencies both audibly shape their own band", "[dsp][engine]")
{
    LancetEngine engine;

    engine.setBandOn (0, true); // Band 1
    engine.setBandFrequencyHz (0, 150.0f);
    engine.setBandGainDb (0, 10.0f);
    engine.setBandRangeDb (0, 0.0f);

    engine.setBandOn (4, true); // Band 5
    engine.setBandFrequencyHz (4, 6000.0f);
    engine.setBandGainDb (4, -10.0f);
    engine.setBandRangeDb (4, 0.0f);

    engine.prepare (makeTestSpec (1));

    auto measureGainDb = [&] (double probeHz)
    {
        LancetEngine local;
        local.setBandOn (0, true);
        local.setBandFrequencyHz (0, 150.0f);
        local.setBandGainDb (0, 10.0f);
        local.setBandOn (4, true);
        local.setBandFrequencyHz (4, 6000.0f);
        local.setBandGainDb (4, -10.0f);
        local.prepare (makeTestSpec (1));

        juce::AudioBuffer<float> reference (1, testBlockSize);
        TestHelpers::fillWithSine (reference, testSampleRate, probeHz, 0.5f);

        juce::AudioBuffer<float> processed;
        processed.makeCopyOf (reference);

        juce::dsp::AudioBlock<float> block (processed);
        local.process (block);

        const auto inRms = TestHelpers::tailRms (reference, settleSamples);
        const auto outRms = TestHelpers::tailRms (processed, settleSamples);
        return juce::Decibels::gainToDecibels (outRms / inRms);
    };

    CHECK (measureGainDb (150.0) > 5.0);
    CHECK (measureGainDb (6000.0) < -5.0);
}

TEST_CASE ("Engine: reset() clears smoother/filter state without crashing", "[dsp][engine]")
{
    LancetEngine engine;

    for (int band = 0; band < LancetEngine::numBands; ++band)
    {
        engine.setBandOn (band, true);
        engine.setBandRangeDb (band, -6.0f);
    }

    engine.prepare (makeTestSpec (2));

    juce::AudioBuffer<float> buffer (2, testBlockSize);
    TestHelpers::fillWithSine (buffer, testSampleRate, 1000.0, 0.8f);

    juce::dsp::AudioBlock<float> block (buffer);
    engine.process (block);

    CHECK_NOTHROW (engine.reset());
    CHECK (TestHelpers::allSamplesFinite (buffer));

    TestHelpers::fillWithSine (buffer, testSampleRate, 1000.0, 0.8f);
    CHECK_NOTHROW (engine.process (block));
    CHECK (TestHelpers::allSamplesFinite (buffer));
}

TEST_CASE ("Engine: zero-sample block is a safe no-op", "[dsp][engine][robustness]")
{
    LancetEngine engine;
    engine.prepare (makeTestSpec (2));

    juce::AudioBuffer<float> buffer (2, 0);
    juce::dsp::AudioBlock<float> block (buffer);

    CHECK_NOTHROW (engine.process (block));
}

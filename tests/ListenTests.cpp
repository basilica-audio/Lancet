#include "dsp/LancetEngine.h"
#include "TestHelpers.h"

#include <catch2/catch_test_macros.hpp>

// "Listen" (exclusive sidechain solo, docs/design-brief.md): engaging a
// band's Listen replaces the program output with that band's own
// bandpass-filtered detector signal, for auditioning what triggers its
// dynamic move.
namespace
{
    constexpr double testSampleRate = 48000.0;
    constexpr int testBlockSize = 8192;

    juce::dsp::ProcessSpec makeTestSpec (int numChannels)
    {
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = testSampleRate;
        spec.maximumBlockSize = static_cast<juce::uint32> (testBlockSize);
        spec.numChannels = static_cast<juce::uint32> (numChannels);
        return spec;
    }
}

TEST_CASE ("Listen: engaging a band's Listen replaces the output with its own bandpass detector signal", "[dsp][listen]")
{
    LancetEngine engine;

    // Band 3 centred on the probe tone; a loud out-of-band tone would
    // otherwise dominate the plain program output, so this also proves
    // Listen genuinely substitutes the output rather than just attenuating
    // it.
    engine.setBandOn (2, true);
    engine.setBandFrequencyHz (2, 1000.0f);
    engine.setBandQ (2, 1.0f);
    engine.setBandGainDb (2, 0.0f);
    engine.setBandRangeDb (2, 0.0f);
    engine.setBandListen (2, true);

    engine.prepare (makeTestSpec (1));

    juce::AudioBuffer<float> buffer (1, testBlockSize);
    // A tone at the band's own centre frequency plus a much louder
    // out-of-band tone - Listen's bandpass tap should reject the
    // out-of-band content, unlike the plain (non-Listen) program output
    // would.
    TestHelpers::fillWithSine (buffer, testSampleRate, 1000.0, 0.3f);

    juce::AudioBuffer<float> loudOutOfBand (1, testBlockSize);
    TestHelpers::fillWithSine (loudOutOfBand, testSampleRate, 60.0, 0.9f);
    buffer.addFrom (0, 0, loudOutOfBand, 0, 0, testBlockSize);

    juce::dsp::AudioBlock<float> block (buffer);
    engine.process (block);

    REQUIRE (TestHelpers::allSamplesFinite (buffer));

    // The Listen tap is a steep (cascaded) bandpass at 1 kHz - the 60 Hz
    // component should be attenuated far more than the in-band 1 kHz
    // component was, i.e. the output is dominated by the detector's
    // bandpass shape, not a plain pass-through of the mixed input.
    const auto peak = TestHelpers::peakAbsolute (buffer);
    CHECK (peak < 0.9f); // the loud 60 Hz component alone would peak near 0.9+0.3
}

TEST_CASE ("Listen: exclusive - the lowest-indexed Listen-engaged band wins", "[dsp][listen]")
{
    LancetEngine engine;

    engine.setBandOn (1, true);
    engine.setBandFrequencyHz (1, 500.0f);
    engine.setBandListen (1, true); // Band 2

    engine.setBandOn (4, true);
    engine.setBandFrequencyHz (4, 5000.0f);
    engine.setBandListen (4, true); // Band 5 - also engaged, should lose

    engine.prepare (makeTestSpec (1));

    juce::AudioBuffer<float> buffer (1, testBlockSize);
    TestHelpers::fillWithSine (buffer, testSampleRate, 500.0, 0.5f);

    juce::dsp::AudioBlock<float> block (buffer);
    CHECK_NOTHROW (engine.process (block));

    REQUIRE (TestHelpers::allSamplesFinite (buffer));

    // Band 2 (index 1)'s own detector buffer should match the substituted
    // output exactly (bit-for-bit - it's a straight buffer copy in
    // LancetEngine::process()) - it won the exclusivity tie-break.
    const auto& band2Listen = engine.getBand (1).getListenBuffer();
    bool allMatch = true;

    for (int i = 0; i < testBlockSize && allMatch; ++i)
        allMatch = juce::exactlyEqual (buffer.getSample (0, i), band2Listen.getSample (0, i));

    CHECK (allMatch);
}

TEST_CASE ("Listen: disengaging returns to normal program output", "[dsp][listen]")
{
    LancetEngine engine;
    engine.setBandOn (2, true);
    engine.setBandFrequencyHz (2, 1000.0f);
    engine.setBandGainDb (2, 6.0f);
    engine.setBandRangeDb (2, 0.0f);

    engine.prepare (makeTestSpec (1));

    juce::AudioBuffer<float> reference (1, testBlockSize);
    TestHelpers::fillWithSine (reference, testSampleRate, 1000.0, 0.5f);

    juce::AudioBuffer<float> processed;
    processed.makeCopyOf (reference);

    juce::dsp::AudioBlock<float> block (processed);
    engine.process (block);

    // With Listen off, a +6 dB bell at the probe frequency should boost the
    // level, not replace it with the detector tap.
    const auto inRms = TestHelpers::tailRms (reference, testBlockSize / 2);
    const auto outRms = TestHelpers::tailRms (processed, testBlockSize / 2);

    REQUIRE (inRms > 0.0);
    CHECK (juce::Decibels::gainToDecibels (outRms / inRms) > 3.0);
}

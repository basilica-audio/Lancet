#include "PluginProcessor.h"
#include "params/ParameterIds.h"
#include "TestHelpers.h"

#include <catch2/catch_test_macros.hpp>

// Guarantee #10 (docs/design-brief.md): "Automation smoothness: full-range
// gain sweep over 1 s produces no sample-to-sample jump > 3 dB (zipper
// guard)."
//
// Deliberately adversarial: rather than many small per-host-block
// automation steps (which would already be smooth regardless of the
// plugin's own internal smoothing, since the *host's* automation
// granularity would dominate), each test applies the *entire* gain range in
// a single abrupt setValueNotifyingHost() call partway through a
// continuous 1-second buffer, stressing whatever internal smoothing (see
// DynamicBand's gainSmoothed / juce::dsp::Gain's own ramp) is actually
// responsible for the guarantee.
//
// Bound derivation: a 0.7-amplitude, 1 kHz sine at 48 kHz has a natural
// per-sample slope of amplitude * 2*pi*f/fs =~ 0.092 even with *no* gain
// change at all. A literal, un-smoothed 3 dB gain pop landing exactly at
// the sine's peak would additionally displace that sample by
// amplitude * (10^(3/20) - 1) =~ 0.289. The threshold below sums both
// (with a small margin) - well under the >= 10x larger jump an actually
// unsmoothed *full-range* (24 dB) pop would produce (~10.4), so this test
// has real bug-catching power against a regression that removed smoothing.
namespace
{
    constexpr double testSampleRate = 48000.0;
    constexpr float testAmplitude = 0.7f;
    const float maxAllowedJump = testAmplitude * (2.0f * juce::MathConstants<float>::pi * 1000.0f / static_cast<float> (testSampleRate))
                                  + testAmplitude * (juce::Decibels::decibelsToGain (3.0f) - 1.0f) + 0.02f;

    // Processes 1 second of continuous 1 kHz sine through `processor`,
    // jumping `param` from normalised 0.0 to 1.0 in a single abrupt call
    // exactly halfway through, and returns the full buffer for inspection.
    juce::AudioBuffer<float> runAbruptFullRangeJump (LancetAudioProcessor& processor, juce::RangedAudioParameter& param)
    {
        constexpr int blockSize = 512;
        constexpr int totalSamples = static_cast<int> (testSampleRate); // 1 second
        constexpr int jumpAtSample = totalSamples / 2;

        param.setValueNotifyingHost (0.0f);

        juce::AudioBuffer<float> fullOutput (1, totalSamples);
        juce::MidiBuffer midi;

        int position = 0;
        bool jumped = false;

        while (position < totalSamples)
        {
            if (! jumped && position >= jumpAtSample)
            {
                param.setValueNotifyingHost (1.0f);
                jumped = true;
            }

            const auto thisBlockSize = juce::jmin (blockSize, totalSamples - position);

            juce::AudioBuffer<float> block (1, thisBlockSize);
            TestHelpers::fillWithSine (block, testSampleRate, 1000.0, testAmplitude, position);

            processor.processBlock (block, midi);

            fullOutput.copyFrom (0, position, block, 0, 0, thisBlockSize);

            position += thisBlockSize;
        }

        return fullOutput;
    }
}

TEST_CASE ("Zipper guard: an abrupt full-range Band Gain jump never produces a >3 dB-equivalent sample-to-sample step",
           "[dsp][zipper]")
{
    LancetAudioProcessor processor;
    processor.prepareToPlay (testSampleRate, 512);

    auto* onParam = processor.apvts.getParameter (ParamIDs::b3On);
    REQUIRE (onParam != nullptr);
    onParam->setValueNotifyingHost (1.0f);

    // Band 3's Freq defaults to 630 Hz, close enough to the 1 kHz probe
    // tone that the boost is substantially applied (not near a null).
    auto* freqParam = processor.apvts.getParameter (ParamIDs::b3Freq);
    REQUIRE (freqParam != nullptr);
    freqParam->setValueNotifyingHost (freqParam->convertTo0to1 (1000.0f));

    auto* gainParam = processor.apvts.getParameter (ParamIDs::b3Gain);
    REQUIRE (gainParam != nullptr);

    const auto output = runAbruptFullRangeJump (processor, *gainParam);

    REQUIRE (TestHelpers::allSamplesFinite (output));
    CHECK (TestHelpers::maxSampleToSampleJump (output) < maxAllowedJump);
}

TEST_CASE ("Zipper guard: an abrupt full-range Output Trim jump never produces a >3 dB-equivalent sample-to-sample step",
           "[dsp][zipper]")
{
    LancetAudioProcessor processor;
    processor.prepareToPlay (testSampleRate, 512);

    auto* outputTrimParam = processor.apvts.getParameter (ParamIDs::outTrim);
    REQUIRE (outputTrimParam != nullptr);

    const auto output = runAbruptFullRangeJump (processor, *outputTrimParam);

    REQUIRE (TestHelpers::allSamplesFinite (output));
    CHECK (TestHelpers::maxSampleToSampleJump (output) < maxAllowedJump);
}

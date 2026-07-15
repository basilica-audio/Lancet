#include "dsp/Detector.h"
#include "TestHelpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

// Guarantee #4 (docs/design-brief.md): "a loud out-of-band tone must not
// trigger the band (>20 dB/oct selectivity at 2 octaves for Q=1)". The
// cascaded (2x) RBJ bandpass measured analytically (see Detector.h's class
// comment) reaches ~-24 dB at 2 octaves for Q=1, comfortably past that bar -
// a single (uncascaded) biquad bandpass only reaches ~-12 dB there, which is
// why Detector cascades two stages rather than one.
namespace
{
    constexpr double testSampleRate = 48000.0;
    constexpr int testBlockSize = 16384;

    juce::dsp::ProcessSpec makeTestSpec (int numChannels)
    {
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = testSampleRate;
        spec.maximumBlockSize = static_cast<juce::uint32> (testBlockSize);
        spec.numChannels = static_cast<juce::uint32> (numChannels);
        return spec;
    }

    // Measures the settled envelope level (dBFS) for a full-block sine tone
    // at `toneHz`, with the detector's bandpass fixed at 1 kHz / Q=1 and a
    // fast attack/release so the envelope has fully settled well before the
    // end of a 16384-sample block.
    float measureSettledLevelDb (double toneHz)
    {
        Detector detector;
        detector.prepare (makeTestSpec (1));
        detector.setFrequencyAndQ (1000.0f, 1.0f);
        detector.setAttackMs (0.5f);
        detector.setReleaseMs (20.0f);

        juce::AudioBuffer<float> buffer (1, testBlockSize);
        TestHelpers::fillWithSine (buffer, testSampleRate, toneHz, 0.8f);

        const juce::dsp::AudioBlock<const float> block (buffer);
        return detector.processSubBlock (block, 0, static_cast<size_t> (testBlockSize));
    }
}

TEST_CASE ("Detector: an in-band tone at the centre frequency settles near full level", "[dsp][detector]")
{
    const auto levelDb = measureSettledLevelDb (1000.0);

    // 0 dB peak-gain RBJ bandpass at its own centre frequency should pass
    // an 0.8-amplitude tone at very close to its original level.
    CHECK (levelDb > -3.0f);
}

TEST_CASE ("Detector: >20 dB isolation two octaves above the centre frequency at Q=1", "[dsp][detector]")
{
    const auto centreLevelDb = measureSettledLevelDb (1000.0);
    const auto twoOctavesAboveLevelDb = measureSettledLevelDb (4000.0);

    CHECK ((centreLevelDb - twoOctavesAboveLevelDb) > 20.0f);
}

TEST_CASE ("Detector: >20 dB isolation two octaves below the centre frequency at Q=1", "[dsp][detector]")
{
    const auto centreLevelDb = measureSettledLevelDb (1000.0);
    const auto twoOctavesBelowLevelDb = measureSettledLevelDb (250.0);

    CHECK ((centreLevelDb - twoOctavesBelowLevelDb) > 20.0f);
}

TEST_CASE ("Detector: reset() clears filter/envelope state (silence after reset reports the noise floor)", "[dsp][detector]")
{
    Detector detector;
    detector.prepare (makeTestSpec (2));
    detector.setFrequencyAndQ (1000.0f, 1.0f);
    detector.setAttackMs (1.0f);
    detector.setReleaseMs (50.0f);

    juce::AudioBuffer<float> buffer (2, testBlockSize);
    TestHelpers::fillWithSine (buffer, testSampleRate, 1000.0, 0.9f);

    const juce::dsp::AudioBlock<const float> loudBlock (buffer);
    const auto loudLevelDb = detector.processSubBlock (loudBlock, 0, static_cast<size_t> (testBlockSize));
    CHECK (loudLevelDb > -6.0f);

    CHECK_NOTHROW (detector.reset());

    juce::AudioBuffer<float> silence (2, testBlockSize);
    silence.clear();

    const juce::dsp::AudioBlock<const float> silentBlock (silence);
    const auto silentLevelDb = detector.processSubBlock (silentBlock, 0, static_cast<size_t> (testBlockSize));

    CHECK (silentLevelDb < -80.0f);
}

TEST_CASE ("Detector: NaN/Inf-free across a denormal-range sweep", "[dsp][detector][robustness]")
{
    Detector detector;
    detector.prepare (makeTestSpec (2));
    detector.setFrequencyAndQ (1000.0f, 1.0f);
    detector.setAttackMs (1.0f);
    detector.setReleaseMs (50.0f);

    juce::AudioBuffer<float> buffer (2, testBlockSize);
    const auto denormalValue = std::numeric_limits<float>::denorm_min() * 4.0f;

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        auto* data = buffer.getWritePointer (channel);

        for (int sample = 0; sample < testBlockSize; ++sample)
            data[sample] = (sample % 2 == 0) ? denormalValue : -denormalValue;
    }

    const juce::dsp::AudioBlock<const float> block (buffer);
    float levelDb = 0.0f;
    CHECK_NOTHROW (levelDb = detector.processSubBlock (block, 0, static_cast<size_t> (testBlockSize)));

    CHECK (std::isfinite (levelDb));
    CHECK (TestHelpers::allSamplesFinite (detector.getListenBuffer()));
}

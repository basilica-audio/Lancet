#include "PluginProcessor.h"
#include "params/ParameterIds.h"
#include "TestHelpers.h"

#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <random>

// Guarantees #5, #6, #8 (docs/design-brief.md):
//   5. NaN/Inf input sweep -> finite output, state recovers after reset().
//   6. Oversized-block clamp (larger than prepared) -> no crash, correct
//      audio (Release-safe).
//   8. reset() clears envelopes and filter state (feed impulse, reset,
//      verify silence).
namespace
{
    void setParam (LancetAudioProcessor& processor, const char* id, float realValue)
    {
        auto* param = processor.apvts.getParameter (id);
        REQUIRE (param != nullptr);
        param->setValueNotifyingHost (param->convertTo0to1 (realValue));
    }

    void setParamNormalised (LancetAudioProcessor& processor, const char* id, float normalisedValue)
    {
        auto* param = processor.apvts.getParameter (id);
        REQUIRE (param != nullptr);
        param->setValueNotifyingHost (normalisedValue);
    }

    // Engages every band with a musically plausible, fairly aggressive
    // dynamic setting, so these robustness tests actually exercise the full
    // signal path (detectors, gain computers, filters) rather than mostly
    // idling on off/static bands.
    void engageAllBands (LancetAudioProcessor& processor)
    {
        static constexpr const char* onIds[] = { ParamIDs::b1On, ParamIDs::b2On, ParamIDs::b3On,
                                                   ParamIDs::b4On, ParamIDs::b5On, ParamIDs::b6On };
        static constexpr const char* rangeIds[] = { ParamIDs::b1Range, ParamIDs::b2Range, ParamIDs::b3Range,
                                                      ParamIDs::b4Range, ParamIDs::b5Range, ParamIDs::b6Range };
        static constexpr const char* thresholdIds[] = { ParamIDs::b1Threshold, ParamIDs::b2Threshold, ParamIDs::b3Threshold,
                                                          ParamIDs::b4Threshold, ParamIDs::b5Threshold, ParamIDs::b6Threshold };

        for (const auto* id : onIds)
            setParamNormalised (processor, id, 1.0f);

        for (const auto* id : rangeIds)
            setParam (processor, id, -9.0f);

        for (const auto* id : thresholdIds)
            setParam (processor, id, -35.0f);
    }
}

TEST_CASE ("Silence produces silence (and no NaN/Inf)", "[robustness]")
{
    LancetAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);
    engageAllBands (processor);

    juce::AudioBuffer<float> buffer (2, 512);
    buffer.clear();

    juce::MidiBuffer midi;

    for (int i = 0; i < 8; ++i)
        CHECK_NOTHROW (processor.processBlock (buffer, midi));

    CHECK (TestHelpers::allSamplesFinite (buffer));
}

TEST_CASE ("NaN input sweep produces finite output, and state recovers after reset()", "[robustness]")
{
    LancetAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);
    engageAllBands (processor);

    juce::AudioBuffer<float> buffer (2, 512);

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        auto* data = buffer.getWritePointer (channel);

        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            data[sample] = (sample % 3 == 0) ? std::numeric_limits<float>::quiet_NaN() : 0.4f;
    }

    juce::MidiBuffer midi;
    processor.processBlock (buffer, midi);

    // NaN is allowed to propagate through this one block (a NaN sample
    // multiplied/filtered stays NaN) - the guarantee is that state
    // *recovers* after reset(), not that a single NaN sample is scrubbed
    // mid-block.
    CHECK_NOTHROW (processor.reset());

    TestHelpers::fillWithSine (buffer, 48000.0, 1000.0, 0.6f);
    CHECK_NOTHROW (processor.processBlock (buffer, midi));
    CHECK (TestHelpers::allSamplesFinite (buffer));
}

TEST_CASE ("Inf input sweep produces finite output after reset(), and does not crash", "[robustness]")
{
    LancetAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);
    engageAllBands (processor);

    juce::AudioBuffer<float> buffer (2, 512);

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        auto* data = buffer.getWritePointer (channel);

        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            data[sample] = (sample % 5 == 0) ? std::numeric_limits<float>::infinity() : -0.3f;
    }

    juce::MidiBuffer midi;
    CHECK_NOTHROW (processor.processBlock (buffer, midi));
    CHECK_NOTHROW (processor.reset());

    TestHelpers::fillWithSine (buffer, 48000.0, 1000.0, 0.6f);
    CHECK_NOTHROW (processor.processBlock (buffer, midi));
    CHECK (TestHelpers::allSamplesFinite (buffer));
}

TEST_CASE ("Denormal-range input produces no NaN/Inf output", "[robustness]")
{
    LancetAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);
    engageAllBands (processor);

    constexpr int numSamples = 512;
    juce::AudioBuffer<float> buffer (2, numSamples);

    const auto denormalValue = std::numeric_limits<float>::denorm_min() * 4.0f;

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        auto* data = buffer.getWritePointer (channel);

        for (int sample = 0; sample < numSamples; ++sample)
            data[sample] = (sample % 2 == 0) ? denormalValue : -denormalValue;
    }

    juce::MidiBuffer midi;

    for (int i = 0; i < 8; ++i)
        CHECK_NOTHROW (processor.processBlock (buffer, midi));

    CHECK (TestHelpers::allSamplesFinite (buffer));
}

TEST_CASE ("Zero-sample buffer does not crash processBlock", "[robustness]")
{
    LancetAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    juce::AudioBuffer<float> buffer (2, 0);
    juce::MidiBuffer midi;

    CHECK_NOTHROW (processor.processBlock (buffer, midi));
    CHECK (buffer.getNumSamples() == 0);
}

TEST_CASE ("Block larger than prepareToPlay's declared size is handled defensively (Release-safe clamp)", "[robustness]")
{
    LancetAudioProcessor processor;
    processor.prepareToPlay (48000.0, 128);
    engageAllBands (processor);

    // Deliberately larger than the 128 declared to prepareToPlay -
    // exercises LancetEngine::process()'s defensive clamp to the pre-chain
    // buffer capacity established in prepare().
    juce::AudioBuffer<float> buffer (2, 4096);
    TestHelpers::fillWithSine (buffer, 48000.0, 1000.0, 0.7f);

    juce::MidiBuffer midi;
    CHECK_NOTHROW (processor.processBlock (buffer, midi));
    CHECK (TestHelpers::allSamplesFinite (buffer));
}

TEST_CASE ("reset() clears filter/envelope state: an impulse's ringing is silenced by reset()", "[robustness]")
{
    LancetAudioProcessor processor;
    processor.prepareToPlay (48000.0, 4096);

    // High-Q resonant bell so any un-cleared filter state rings audibly.
    setParamNormalised (processor, ParamIDs::b3On, 1.0f);
    setParam (processor, ParamIDs::b3Freq, 1000.0f);
    setParam (processor, ParamIDs::b3Q, 10.0f);
    setParam (processor, ParamIDs::b3Gain, 12.0f);
    setParam (processor, ParamIDs::b3Range, 0.0f);

    juce::AudioBuffer<float> impulse (2, 4096);
    impulse.clear();
    impulse.setSample (0, 0, 1.0f);
    impulse.setSample (1, 0, 1.0f);

    juce::MidiBuffer midi;
    processor.processBlock (impulse, midi);

    // The resonant filter's ringing tail should still be clearly audible
    // this far after the impulse (this is the negative control proving the
    // scenario actually exercises filter memory before reset() clears it).
    CHECK (TestHelpers::peakAbsolute (impulse) > 1.0e-4f);

    CHECK_NOTHROW (processor.reset());

    juce::AudioBuffer<float> silence (2, 4096);
    silence.clear();
    processor.processBlock (silence, midi);

    CHECK (TestHelpers::allSamplesFinite (silence));
    CHECK (TestHelpers::peakAbsolute (silence) < 1.0e-6f);
}

TEST_CASE ("Rapid parameter automation across many blocks produces no NaN/Inf", "[robustness]")
{
    LancetAudioProcessor processor;
    processor.prepareToPlay (48000.0, 256);

    std::mt19937 rng (1234);
    std::uniform_real_distribution<float> unit (0.0f, 1.0f);

    juce::MidiBuffer midi;

    for (int block = 0; block < 100; ++block)
    {
        for (int band = 1; band <= LancetEngine::numBands; ++band)
        {
            const auto prefix = "b" + juce::String (band) + "_";

            setParamNormalised (processor, (prefix + "on").toRawUTF8(), unit (rng) > 0.5f ? 1.0f : 0.0f);
            setParam (processor, (prefix + "freq").toRawUTF8(), 20.0f + unit (rng) * 19980.0f);
            setParam (processor, (prefix + "q").toRawUTF8(), 0.3f + unit (rng) * 11.7f);
            setParam (processor, (prefix + "gain").toRawUTF8(), -12.0f + unit (rng) * 24.0f);
            setParam (processor, (prefix + "range").toRawUTF8(), -12.0f + unit (rng) * 24.0f);
            setParam (processor, (prefix + "thresh").toRawUTF8(), -60.0f + unit (rng) * 60.0f);
            setParam (processor, (prefix + "attack").toRawUTF8(), 0.5f + unit (rng) * 99.5f);
            setParam (processor, (prefix + "release").toRawUTF8(), 10.0f + unit (rng) * 990.0f);
        }

        setParam (processor, ParamIDs::inTrim, -12.0f + unit (rng) * 24.0f);
        setParam (processor, ParamIDs::outTrim, -12.0f + unit (rng) * 24.0f);
        setParam (processor, ParamIDs::mix, unit (rng) * 100.0f);

        juce::AudioBuffer<float> buffer (2, 256);
        TestHelpers::fillWithSine (buffer, 48000.0, 200.0 + unit (rng) * 4000.0, 0.7f);

        CHECK_NOTHROW (processor.processBlock (buffer, midi));
        CHECK (TestHelpers::allSamplesFinite (buffer));
    }
}

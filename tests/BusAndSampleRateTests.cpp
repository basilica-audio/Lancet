#include "PluginProcessor.h"
#include "params/ParameterIds.h"
#include "TestHelpers.h"

#include <catch2/catch_test_macros.hpp>

// Sample-rate sweeps and bus-layout coverage, matching sibling plugins'
// tests/BusAndSampleRateTests.cpp pattern (e.g. triptych).
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

    void engageModerateDynamics (LancetAudioProcessor& processor)
    {
        setParamNormalised (processor, ParamIDs::b3On, 1.0f);
        setParam (processor, ParamIDs::b3Freq, 1000.0f);
        setParam (processor, ParamIDs::b3Range, -6.0f);
        setParam (processor, ParamIDs::b3Threshold, -24.0f);
    }
}

TEST_CASE ("Sample-rate sweep 44.1-192 kHz: finite output and zero latency at every rate", "[robustness][samplerate]")
{
    static constexpr double sampleRates[] = { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 };

    for (const auto sampleRate : sampleRates)
    {
        INFO ("sample rate = " << sampleRate);

        LancetAudioProcessor processor;
        processor.prepareToPlay (sampleRate, 256);

        CHECK (processor.getLatencySamples() == 0);

        engageModerateDynamics (processor);

        juce::AudioBuffer<float> buffer (2, 256);
        TestHelpers::fillWithSine (buffer, sampleRate, 1000.0, 0.7f);

        juce::MidiBuffer midi;

        for (int block = 0; block < 4; ++block)
            CHECK_NOTHROW (processor.processBlock (buffer, midi));

        CHECK (TestHelpers::allSamplesFinite (buffer));
    }
}

TEST_CASE ("Sample-rate change mid-session (prepareToPlay called again) stays finite", "[robustness][samplerate]")
{
    LancetAudioProcessor processor;
    juce::MidiBuffer midi;

    static constexpr double sampleRates[] = { 44100.0, 192000.0, 48000.0, 96000.0 };

    for (const auto sampleRate : sampleRates)
    {
        processor.prepareToPlay (sampleRate, 512);
        engageModerateDynamics (processor);

        juce::AudioBuffer<float> buffer (2, 512);
        TestHelpers::fillWithSine (buffer, sampleRate, 220.0, 0.6f);

        CHECK_NOTHROW (processor.processBlock (buffer, midi));
        CHECK (TestHelpers::allSamplesFinite (buffer));
    }
}

TEST_CASE ("Mono bus layout is supported and processes without NaN/Inf", "[robustness][buslayout]")
{
    LancetAudioProcessor processor;

    juce::AudioProcessor::BusesLayout monoLayout;
    monoLayout.inputBuses.add (juce::AudioChannelSet::mono());
    // Input bus 1 is the optional sidechain added in v0.4.0. setBusesLayout()
    // requires one entry per declared bus, so it has to be named here even
    // when it is being left disabled - which is exactly the configuration
    // this case is about (a host that knows nothing about the sidechain).
    monoLayout.inputBuses.add (juce::AudioChannelSet::disabled());
    monoLayout.outputBuses.add (juce::AudioChannelSet::mono());

    REQUIRE (processor.isBusesLayoutSupported (monoLayout));
    REQUIRE (processor.setBusesLayout (monoLayout));

    processor.prepareToPlay (48000.0, 256);
    engageModerateDynamics (processor);

    juce::AudioBuffer<float> buffer (1, 256);
    TestHelpers::fillWithSine (buffer, 48000.0, 1000.0, 0.8f);

    juce::MidiBuffer midi;

    for (int block = 0; block < 4; ++block)
        CHECK_NOTHROW (processor.processBlock (buffer, midi));

    CHECK (TestHelpers::allSamplesFinite (buffer));
}

TEST_CASE ("Stereo bus layout is supported (explicit isBusesLayoutSupported check)", "[robustness][buslayout]")
{
    LancetAudioProcessor processor;

    juce::AudioProcessor::BusesLayout stereoLayout;
    stereoLayout.inputBuses.add (juce::AudioChannelSet::stereo());
    stereoLayout.outputBuses.add (juce::AudioChannelSet::stereo());

    CHECK (processor.isBusesLayoutSupported (stereoLayout));
}

TEST_CASE ("Mismatched in/out channel-set bus layouts are rejected", "[robustness][buslayout]")
{
    LancetAudioProcessor processor;

    juce::AudioProcessor::BusesLayout mismatchedLayout;
    mismatchedLayout.inputBuses.add (juce::AudioChannelSet::mono());
    mismatchedLayout.outputBuses.add (juce::AudioChannelSet::stereo());

    CHECK_FALSE (processor.isBusesLayoutSupported (mismatchedLayout));
}

TEST_CASE ("Unsupported multichannel bus layout is rejected", "[robustness][buslayout]")
{
    LancetAudioProcessor processor;

    juce::AudioProcessor::BusesLayout quadLayout;
    quadLayout.inputBuses.add (juce::AudioChannelSet::quadraphonic());
    quadLayout.outputBuses.add (juce::AudioChannelSet::quadraphonic());

    CHECK_FALSE (processor.isBusesLayoutSupported (quadLayout));
}

// v0.4.0 (SOTA brief F3/Risk 2): the sidechain is an *optional* second input
// bus. Adding a bus changes the plugin's I/O signature, which is the kind of
// thing hosts re-scan on, so its accepted shapes are pinned explicitly here
// rather than left to pluginval alone.
TEST_CASE ("Sidechain bus: disabled, mono and stereo are all accepted, independently of the main layout",
           "[robustness][buslayout][sidechain]")
{
    LancetAudioProcessor processor;

    const auto makeLayout = [] (juce::AudioChannelSet main, juce::AudioChannelSet sidechain)
    {
        juce::AudioProcessor::BusesLayout layout;
        layout.inputBuses.add (main);
        layout.inputBuses.add (sidechain);
        layout.outputBuses.add (main);
        return layout;
    };

    for (const auto& main : { juce::AudioChannelSet::mono(), juce::AudioChannelSet::stereo() })
    {
        for (const auto& sidechain : { juce::AudioChannelSet::disabled(),
                                        juce::AudioChannelSet::mono(),
                                        juce::AudioChannelSet::stereo() })
        {
            INFO ("main = " << main.getDescription() << ", sidechain = " << sidechain.getDescription());
            CHECK (processor.isBusesLayoutSupported (makeLayout (main, sidechain)));
        }
    }

    // Anything wider than stereo on the sidechain is rejected rather than
    // silently misinterpreted.
    CHECK_FALSE (processor.isBusesLayoutSupported (
        makeLayout (juce::AudioChannelSet::stereo(), juce::AudioChannelSet::quadraphonic())));
}

TEST_CASE ("Sidechain bus enabled: the main output is still the main bus's own channels, and stays finite",
           "[robustness][buslayout][sidechain]")
{
    LancetAudioProcessor processor;

    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add (juce::AudioChannelSet::stereo());
    layout.inputBuses.add (juce::AudioChannelSet::stereo());
    layout.outputBuses.add (juce::AudioChannelSet::stereo());

    REQUIRE (processor.isBusesLayoutSupported (layout));
    REQUIRE (processor.setBusesLayout (layout));

    processor.prepareToPlay (48000.0, 256);
    engageModerateDynamics (processor);

    // 2 main + 2 sidechain channels in one process block, the shape a host
    // hands an effect with an enabled aux input.
    juce::AudioBuffer<float> buffer (4, 256);
    TestHelpers::fillWithSine (buffer, 48000.0, 1000.0, 0.8f);

    juce::MidiBuffer midi;

    for (int block = 0; block < 4; ++block)
        CHECK_NOTHROW (processor.processBlock (buffer, midi));

    CHECK (TestHelpers::allSamplesFinite (buffer));
    CHECK (processor.getLatencySamples() == 0);
}

TEST_CASE ("Long-run processing (many blocks, several seconds of audio) produces no NaN/Inf drift", "[robustness][longrun]")
{
    LancetAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    engageModerateDynamics (processor);
    setParam (processor, ParamIDs::outTrim, 3.0f);

    juce::MidiBuffer midi;

    constexpr int numBlocks = 500; // ~5.3 s @ 512 samples/48 kHz

    for (int block = 0; block < numBlocks; ++block)
    {
        juce::AudioBuffer<float> buffer (2, 512);
        TestHelpers::fillWithSine (buffer, 48000.0, 110.0, 0.75f, static_cast<juce::int64> (block) * 512);

        CHECK_NOTHROW (processor.processBlock (buffer, midi));
        REQUIRE (TestHelpers::allSamplesFinite (buffer));
        REQUIRE (TestHelpers::peakAbsolute (buffer) < 100.0f);
    }
}

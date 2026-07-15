#include "PluginProcessor.h"
#include "dsp/LancetEngine.h"

#include <catch2/catch_test_macros.hpp>

// Guarantee #9 (docs/design-brief.md): "getLatencySamples() == 0." Every
// filter in the engine (six bell/shelf bands, six Detector bandpasses) is a
// minimum-phase IIR biquad with no lookahead, so Lancet never adds latency -
// unlike, e.g. an oversampled clipper, there is no dry-path delay
// compensation to verify here.
TEST_CASE ("getLatencySamples() reports zero latency, before and after prepareToPlay", "[latency]")
{
    LancetAudioProcessor processor;

    CHECK (processor.getLatencySamples() == 0);

    processor.prepareToPlay (48000.0, 512);

    CHECK (processor.getLatencySamples() == 0);
    CHECK (LancetEngine::getLatencySamples() == 0);
}

TEST_CASE ("Latency stays zero across sample-rate and block-size changes", "[latency]")
{
    LancetAudioProcessor processor;

    processor.prepareToPlay (44100.0, 256);
    CHECK (processor.getLatencySamples() == 0);

    processor.prepareToPlay (96000.0, 1024);
    CHECK (processor.getLatencySamples() == 0);

    processor.prepareToPlay (192000.0, 32);
    CHECK (processor.getLatencySamples() == 0);
}

TEST_CASE ("Latency stays zero with every band engaged", "[latency]")
{
    LancetAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    for (const auto* onId : { "b1_on", "b2_on", "b3_on", "b4_on", "b5_on", "b6_on" })
    {
        auto* param = processor.apvts.getParameter (onId);
        REQUIRE (param != nullptr);
        param->setValueNotifyingHost (1.0f);
    }

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    processor.processBlock (buffer, midi);

    CHECK (processor.getLatencySamples() == 0);
}

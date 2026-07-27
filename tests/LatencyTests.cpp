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

//==============================================================================
// v0.4.0 (SOTA brief T10): the release adds an external sidechain bus and an
// anti-aliased saturator, and neither is allowed to cost latency. ADAA was
// chosen over oversampling precisely so that it does not, and the sidechain
// deliberately has no alignment delay (the host is responsible for aligning
// it, as comparable dynamic EQs require).
TEST_CASE ("Latency stays zero with the sidechain bus enabled and every band engaged", "[latency][sidechain]")
{
    LancetAudioProcessor processor;

    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add (juce::AudioChannelSet::stereo());
    layout.inputBuses.add (juce::AudioChannelSet::stereo());
    layout.outputBuses.add (juce::AudioChannelSet::stereo());
    REQUIRE (processor.setBusesLayout (layout));

    processor.prepareToPlay (48000.0, 512);

    for (const auto* onId : { "b1_on", "b2_on", "b3_on", "b4_on", "b5_on", "b6_on" })
    {
        auto* param = processor.apvts.getParameter (onId);
        REQUIRE (param != nullptr);
        param->setValueNotifyingHost (1.0f);
    }

    // Every new v0.4.0 switch engaged at once, plus Saturation.
    for (const auto* choiceId : { "b1_scSource", "b2_scSource", "b3_scSource", "b4_scSource", "b5_scSource", "b6_scSource",
                                   "b1_scMode", "b2_scMode", "b3_scMode", "b4_scMode", "b5_scMode", "b6_scMode",
                                   "b1_sat", "b2_sat", "b3_sat", "b4_sat", "b5_sat", "b6_sat" })
    {
        auto* param = processor.apvts.getParameter (choiceId);
        REQUIRE (param != nullptr);
        param->setValueNotifyingHost (1.0f);
    }

    juce::AudioBuffer<float> buffer (4, 512);
    buffer.clear();
    juce::MidiBuffer midi;
    processor.processBlock (buffer, midi);

    CHECK (processor.getLatencySamples() == 0);
    CHECK (LancetEngine::getLatencySamples() == 0);
}

TEST_CASE ("Zero latency is real: an impulse comes back with its peak at sample 0", "[latency]")
{
    // A stronger statement than getLatencySamples() == 0, which is only what
    // the plugin *reports*. A band boosting at a low frequency is the case
    // most likely to smear an impulse if any delay had crept in.
    LancetAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    const auto setParam = [&processor] (const char* id, float realValue)
    {
        auto* param = processor.apvts.getParameter (id);
        REQUIRE (param != nullptr);
        param->setValueNotifyingHost (param->convertTo0to1 (realValue));
    };

    auto* on = processor.apvts.getParameter ("b3_on");
    REQUIRE (on != nullptr);
    on->setValueNotifyingHost (1.0f);
    setParam ("b3_freq", 200.0f);
    setParam ("b3_q", 1.0f);
    setParam ("b3_gain", 12.0f);

    // Let the static-gain smoother settle before the impulse arrives.
    juce::AudioBuffer<float> silence (2, 512);
    juce::MidiBuffer midi;

    for (int block = 0; block < 20; ++block)
    {
        silence.clear();
        processor.processBlock (silence, midi);
    }

    juce::AudioBuffer<float> impulse (2, 512);
    impulse.clear();
    impulse.setSample (0, 0, 1.0f);
    impulse.setSample (1, 0, 1.0f);

    processor.processBlock (impulse, midi);

    int peakIndex = 0;
    float peakValue = 0.0f;

    for (int i = 0; i < impulse.getNumSamples(); ++i)
    {
        const auto magnitude = std::abs (impulse.getSample (0, i));

        if (magnitude > peakValue)
        {
            peakValue = magnitude;
            peakIndex = i;
        }
    }

    INFO ("impulse peak of " << peakValue << " at sample " << peakIndex);
    CHECK (peakIndex == 0);
    CHECK (peakValue > 0.5f);
}

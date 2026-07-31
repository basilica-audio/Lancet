#include "PluginProcessor.h"
#include "dsp/LancetEngine.h"
#include "params/ParameterIds.h"
#include "TestHelpers.h"

#include <catch2/catch_test_macros.hpp>

#include <random>

// Suite-wide hardening wave (2026-07-31): sample-rate-matrix reprepare
// coverage. LatencyTests.cpp's "Latency stays zero across sample-rate and
// block-size changes" case moves 44.1k -> 96k -> 192k once, with no
// processing or parameter movement between reprepares and no assertion
// beyond getLatencySamples(). Hosts do reprepare repeatedly (sample-rate
// changes, buffer-size renegotiation), each time expecting a clean engine
// reset, so this exercises a full sequence - prepare, process with
// parameter churn, reprepare at a new rate with both a small and a large
// block, process again - on a single long-lived processor instance, and
// checks state (APVTS parameter values) survives every reprepare
// unperturbed, which none of the existing latency/robustness tests do.
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

    float getParamNormalised (LancetAudioProcessor& processor, const char* id)
    {
        auto* param = processor.apvts.getParameter (id);
        REQUIRE (param != nullptr);
        return param->getValue();
    }

    // Engages every band and churns Freq/Range/Attack/Release across a few
    // blocks - the "process with parameter churn" step between reprepares.
    // Mirrors AllocationTests.cpp's steady-state sweep pattern.
    void churnAndProcess (LancetAudioProcessor& processor,
                           double sampleRate,
                           int blockSize,
                           std::mt19937& rng,
                           int numBlocks = 6)
    {
        std::uniform_real_distribution<float> unit (0.0f, 1.0f);
        juce::MidiBuffer midi;

        for (int block = 0; block < numBlocks; ++block)
        {
            for (int band = 1; band <= LancetEngine::numBands; ++band)
            {
                const auto prefix = "b" + juce::String (band) + "_";
                setParam (processor, (prefix + "freq").toRawUTF8(), 200.0f + unit (rng) * 8000.0f);
                setParam (processor, (prefix + "range").toRawUTF8(), -12.0f + unit (rng) * 24.0f);
                setParam (processor, (prefix + "attack").toRawUTF8(), 0.1f + unit (rng) * 100.0f);
                setParam (processor, (prefix + "release").toRawUTF8(), 5.0f + unit (rng) * 500.0f);
            }

            juce::AudioBuffer<float> buffer (2, blockSize);

            if (blockSize > 0)
                TestHelpers::fillWithSine (buffer, sampleRate, 220.0 + unit (rng) * 2000.0, 0.7f);

            CHECK_NOTHROW (processor.processBlock (buffer, midi));

            if (blockSize > 0)
                CHECK (TestHelpers::allSamplesFinite (buffer));
        }
    }
}

TEST_CASE ("Sample-rate matrix: reprepare 44.1k -> 96k -> 192k (small and large blocks) "
           "survives with latency staying zero and parameter state intact",
           "[latency][sample-rate-matrix][reprepare]")
{
    LancetAudioProcessor processor;
    std::mt19937 rng (4242);

    for (int band = 1; band <= LancetEngine::numBands; ++band)
    {
        const auto prefix = "b" + juce::String (band) + "_";
        setParamNormalised (processor, (prefix + "on").toRawUTF8(), 1.0f);
        setParam (processor, (prefix + "q").toRawUTF8(), 1.0f);
        setParam (processor, (prefix + "gain").toRawUTF8(), 0.0f);
        setParam (processor, (prefix + "thresh").toRawUTF8(), -30.0f);
        setParamNormalised (processor, (prefix + "autoRelease").toRawUTF8(), 1.0f);
        setParamNormalised (processor, (prefix + "gainQ").toRawUTF8(), 1.0f);
        setParamNormalised (processor, (prefix + "sat").toRawUTF8(), 1.0f);
    }

    // A state-survival marker: an explicit, non-default Band 3 Gain value
    // set once, before the very first prepare(), that must still read back
    // identically after every reprepare below - reprepareToPlay() must
    // never reset APVTS-owned parameter state, only the DSP engine's
    // internal filter/detector buffers. Compared via the normalised [0,1]
    // representation directly (rather than round-tripping through
    // convertTo0to1/convertFrom0to1) so there is no ambiguity about which
    // side introduces rounding.
    setParam (processor, ParamIDs::b3Gain, 6.5f);
    const auto markerGainNormalised = getParamNormalised (processor, ParamIDs::b3Gain);

    // --- 44.1 kHz, the starting rate ------------------------------------
    processor.prepareToPlay (44100.0, 512);
    CHECK (processor.getLatencySamples() == 0);
    CHECK (LancetEngine::getLatencySamples() == 0);
    churnAndProcess (processor, 44100.0, 512, rng);
    CHECK (getParamNormalised (processor, ParamIDs::b3Gain) == markerGainNormalised);

    // --- 96 kHz: small block, then large block --------------------------
    processor.prepareToPlay (96000.0, 32);
    CHECK (processor.getLatencySamples() == 0);
    churnAndProcess (processor, 96000.0, 32, rng);
    CHECK (getParamNormalised (processor, ParamIDs::b3Gain) == markerGainNormalised);

    processor.prepareToPlay (96000.0, 8192);
    CHECK (processor.getLatencySamples() == 0);
    churnAndProcess (processor, 96000.0, 8192, rng);
    CHECK (getParamNormalised (processor, ParamIDs::b3Gain) == markerGainNormalised);

    // --- 192 kHz: small block, then large block --------------------------
    processor.prepareToPlay (192000.0, 16);
    CHECK (processor.getLatencySamples() == 0);
    churnAndProcess (processor, 192000.0, 16, rng);
    CHECK (getParamNormalised (processor, ParamIDs::b3Gain) == markerGainNormalised);

    processor.prepareToPlay (192000.0, 16384);
    CHECK (processor.getLatencySamples() == 0);
    churnAndProcess (processor, 192000.0, 16384, rng);
    CHECK (getParamNormalised (processor, ParamIDs::b3Gain) == markerGainNormalised);

    // Finally, back down to 44.1 kHz (round trip): latency must still be
    // zero and a fresh block must still come out finite, proving no
    // reprepare along the way left the engine in a state that depends on
    // prepare *history* rather than just the current spec.
    processor.prepareToPlay (44100.0, 512);
    CHECK (processor.getLatencySamples() == 0);
    churnAndProcess (processor, 44100.0, 512, rng);
    CHECK (getParamNormalised (processor, ParamIDs::b3Gain) == markerGainNormalised);

    juce::AudioBuffer<float> finalBuffer (2, 512);
    TestHelpers::fillWithSine (finalBuffer, 44100.0, 1000.0, 0.9f);
    juce::MidiBuffer midi;
    processor.processBlock (finalBuffer, midi);
    CHECK (TestHelpers::allSamplesFinite (finalBuffer));
}

TEST_CASE ("Sample-rate matrix: reprepare with a zero-sample buffer immediately after does not crash",
           "[latency][sample-rate-matrix][reprepare]")
{
    // A narrower, cheap companion to the case above: some hosts hand over a
    // zero-length buffer on the very first callback after a reprepare
    // (buffer-size renegotiation mid-stream).
    LancetAudioProcessor processor;

    for (const auto* onId : { "b1_on", "b2_on", "b3_on", "b4_on", "b5_on", "b6_on" })
    {
        auto* param = processor.apvts.getParameter (onId);
        REQUIRE (param != nullptr);
        param->setValueNotifyingHost (1.0f);
    }

    juce::MidiBuffer midi;

    for (double rate : { 44100.0, 96000.0, 192000.0 })
    {
        for (int blockSize : { 1, 4096 })
        {
            processor.prepareToPlay (rate, blockSize);
            CHECK (processor.getLatencySamples() == 0);

            juce::AudioBuffer<float> zeroBuffer (2, 0);
            CHECK_NOTHROW (processor.processBlock (zeroBuffer, midi));
            CHECK (zeroBuffer.getNumSamples() == 0);

            juce::AudioBuffer<float> normalBuffer (2, blockSize);
            TestHelpers::fillWithSine (normalBuffer, rate, 500.0, 0.5f);
            CHECK_NOTHROW (processor.processBlock (normalBuffer, midi));
            CHECK (TestHelpers::allSamplesFinite (normalBuffer));
        }
    }
}

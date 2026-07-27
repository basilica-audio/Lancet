#include "dsp/LancetEngine.h"
#include "PluginProcessor.h"
#include "params/ParameterIds.h"
#include "TestHelpers.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>

// v0.4.0 (SOTA brief F3/F4, tests T6/T7): external sidechain input and
// Split/Wide detection - the two Waves-F6-parity controls this release adds.
//
// Both are per-band and both default to the pre-v0.4.0 behaviour, so the
// assertions below are as much about what does NOT change (Internal/Split
// behaves exactly as before; a missing sidechain bus falls back rather than
// silencing the band) as about the new routing working.
namespace
{
    constexpr double testSampleRate = 48000.0;
    constexpr float centreFrequencyHz = 1000.0f;
    constexpr int blockSamples = 512;
    constexpr int bandIndex = 2; // Band 3 - always Bell, no Type parameter

    juce::dsp::ProcessSpec makeSpec (int numChannels = 1, int maxBlock = blockSamples)
    {
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = testSampleRate;
        spec.maximumBlockSize = static_cast<juce::uint32> (maxBlock);
        spec.numChannels = static_cast<juce::uint32> (numChannels);
        return spec;
    }

    void configureCuttingBand (LancetEngine& engine, float thresholdDb)
    {
        engine.setBandOn (bandIndex, true);
        engine.setBandFrequencyHz (bandIndex, centreFrequencyHz);
        engine.setBandQ (bandIndex, 1.0f);
        engine.setBandGainDb (bandIndex, 0.0f);
        engine.setBandRangeDb (bandIndex, -12.0f);
        engine.setBandThresholdDb (bandIndex, thresholdDb);
        engine.setBandAttackMs (bandIndex, 1.0f);
        engine.setBandReleaseMs (bandIndex, 50.0f);
        engine.setInputTrimDb (0.0f);
        engine.setOutputTrimDb (0.0f);
        engine.setMixPercent (100.0f);
    }

    // Runs `numBlocks` blocks of a steady main tone (plus, optionally, a
    // steady sidechain tone) through the engine and returns the settled
    // magnitude of the band's applied dynamic gain, in dB.
    float measureSettledGrDb (LancetEngine& engine,
                               double mainToneHz,
                               float mainAmplitude,
                               bool provideSidechain,
                               double sidechainToneHz = static_cast<double> (centreFrequencyHz),
                               float sidechainAmplitude = 0.0f,
                               int numBlocks = 40)
    {
        juce::AudioBuffer<float> main (1, blockSamples);
        juce::AudioBuffer<float> sidechain (1, blockSamples);

        for (int block = 0; block < numBlocks; ++block)
        {
            const auto offset = static_cast<juce::int64> (block) * blockSamples;

            TestHelpers::fillWithSine (main, testSampleRate, mainToneHz, mainAmplitude, offset);
            juce::dsp::AudioBlock<float> mainBlock (main);

            if (provideSidechain)
            {
                TestHelpers::fillWithSine (sidechain, testSampleRate, sidechainToneHz, sidechainAmplitude, offset);
                const juce::dsp::AudioBlock<const float> sidechainBlock (sidechain);
                engine.process (mainBlock, sidechainBlock);
            }
            else
            {
                engine.process (mainBlock);
            }
        }

        return std::abs (engine.getLastAppliedDynamicGainDb (bandIndex));
    }
}

//==============================================================================
// T6 - Split vs Wide detection.

TEST_CASE ("Split detection ignores an out-of-band tone; Wide detection follows it", "[dsp][sidechain][detector]")
{
    // Two octaves above the band centre, 20 dB over threshold. In Split the
    // cascaded bandpass attenuates it by ~24 dB (see DetectorTests), so the
    // band must stay essentially still; in Wide there is no bandpass at all,
    // so the band must engage hard.
    constexpr double outOfBandHz = 4.0 * centreFrequencyHz;
    constexpr float amplitude = 0.5f;

    const auto toneDbfs = juce::Decibels::gainToDecibels (amplitude);

    LancetEngine splitEngine;
    configureCuttingBand (splitEngine, toneDbfs - 20.0f);
    splitEngine.setBandDetectorWide (bandIndex, false);
    splitEngine.prepare (makeSpec());

    const auto splitGrDb = measureSettledGrDb (splitEngine, outOfBandHz, amplitude, false);

    LancetEngine wideEngine;
    configureCuttingBand (wideEngine, toneDbfs - 20.0f);
    wideEngine.setBandDetectorWide (bandIndex, true);
    wideEngine.prepare (makeSpec());

    const auto wideGrDb = measureSettledGrDb (wideEngine, outOfBandHz, amplitude, false);

    INFO ("Split GR = " << splitGrDb << " dB, Wide GR = " << wideGrDb << " dB");
    CHECK (splitGrDb < 0.1f);
    CHECK (wideGrDb >= 3.0f);
}

TEST_CASE ("Split and Wide agree when the whole signal is in band", "[dsp][sidechain][detector]")
{
    // A sanity counterpart to the case above: the two modes must only differ
    // because of out-of-band content, not because Wide is systematically
    // hotter. At the band centre the bandpass is unity gain, so both modes
    // should land on the same gain reduction.
    constexpr float amplitude = 0.5f;
    const auto toneDbfs = juce::Decibels::gainToDecibels (amplitude);

    LancetEngine splitEngine;
    configureCuttingBand (splitEngine, toneDbfs - 6.0f);
    splitEngine.setBandDetectorWide (bandIndex, false);
    splitEngine.prepare (makeSpec());
    const auto splitGrDb = measureSettledGrDb (splitEngine, centreFrequencyHz, amplitude, false);

    LancetEngine wideEngine;
    configureCuttingBand (wideEngine, toneDbfs - 6.0f);
    wideEngine.setBandDetectorWide (bandIndex, true);
    wideEngine.prepare (makeSpec());
    const auto wideGrDb = measureSettledGrDb (wideEngine, centreFrequencyHz, amplitude, false);

    INFO ("Split GR = " << splitGrDb << " dB, Wide GR = " << wideGrDb << " dB");
    CHECK (std::abs (splitGrDb - wideGrDb) < 0.5f);
}

//==============================================================================
// T7 - External sidechain.

TEST_CASE ("External SC Source detects from the sidechain bus, Internal ignores it", "[dsp][sidechain]")
{
    // Main programme sits 30 dB below threshold, so nothing the band hears
    // internally can move it. The sidechain carries a loud tone at the band
    // centre.
    constexpr float quietMainAmplitude = 0.01f; // -40 dBFS
    constexpr float loudSidechainAmplitude = 1.0f;
    constexpr float thresholdDb = -20.0f;

    LancetEngine externalEngine;
    configureCuttingBand (externalEngine, thresholdDb);
    externalEngine.setBandSidechainExternal (bandIndex, true);
    externalEngine.prepare (makeSpec());

    const auto externalGrDb = measureSettledGrDb (externalEngine, centreFrequencyHz, quietMainAmplitude,
                                                   true, centreFrequencyHz, loudSidechainAmplitude);

    LancetEngine internalEngine;
    configureCuttingBand (internalEngine, thresholdDb);
    internalEngine.setBandSidechainExternal (bandIndex, false);
    internalEngine.prepare (makeSpec());

    const auto internalGrDb = measureSettledGrDb (internalEngine, centreFrequencyHz, quietMainAmplitude,
                                                   true, centreFrequencyHz, loudSidechainAmplitude);

    INFO ("External GR = " << externalGrDb << " dB, Internal GR = " << internalGrDb << " dB");
    CHECK (externalGrDb >= 6.0f);
    CHECK (internalGrDb < 0.1f);
}

TEST_CASE ("External SC Source with no sidechain present falls back to Internal", "[dsp][sidechain]")
{
    // Selecting External in a host with no sidechain routing (or with the bus
    // left disabled) must be inaudible, not silent and not NaN - the band
    // simply keeps using the pre-chain tap.
    constexpr float amplitude = 0.5f;
    const auto toneDbfs = juce::Decibels::gainToDecibels (amplitude);

    LancetEngine fallbackEngine;
    configureCuttingBand (fallbackEngine, toneDbfs - 12.0f);
    fallbackEngine.setBandSidechainExternal (bandIndex, true);
    fallbackEngine.prepare (makeSpec());
    const auto fallbackGrDb = measureSettledGrDb (fallbackEngine, centreFrequencyHz, amplitude, false);

    LancetEngine internalEngine;
    configureCuttingBand (internalEngine, toneDbfs - 12.0f);
    internalEngine.setBandSidechainExternal (bandIndex, false);
    internalEngine.prepare (makeSpec());
    const auto internalGrDb = measureSettledGrDb (internalEngine, centreFrequencyHz, amplitude, false);

    INFO ("fallback GR = " << fallbackGrDb << " dB, internal GR = " << internalGrDb << " dB");
    CHECK (std::isfinite (fallbackGrDb));
    CHECK (fallbackGrDb > 1.0f); // it really did detect something
    CHECK (std::abs (fallbackGrDb - internalGrDb) < 0.01f);
}

TEST_CASE ("Toggling SC Source and SC Mode every 1024 samples for 5 s stays click-free and finite",
           "[dsp][sidechain][robustness]")
{
    // The state-continuity guarantee: neither switch resets the envelope, and
    // Wide -> Split re-primes the bandpass rather than resuming from stale
    // state. The bound is the same one tests/ZipperTests.cpp derives - a
    // 0.7-amplitude 1 kHz sine's own per-sample slope plus a 3 dB pop's worth
    // of displacement - so a switch that produced an audible click would have
    // to stay under what a 3 dB gain step would do.
    constexpr int totalSamples = static_cast<int> (5.0 * testSampleRate);
    constexpr int toggleEvery = 1024;
    constexpr float amplitude = 0.7f;

    const auto maxAllowedJump = amplitude * (2.0f * juce::MathConstants<float>::pi * 1000.0f / static_cast<float> (testSampleRate))
                                 + amplitude * (juce::Decibels::decibelsToGain (3.0f) - 1.0f) + 0.02f;

    LancetEngine engine;
    configureCuttingBand (engine, -30.0f);
    engine.prepare (makeSpec (1, toggleEvery));

    juce::AudioBuffer<float> output (1, totalSamples);
    juce::AudioBuffer<float> main (1, toggleEvery);
    juce::AudioBuffer<float> sidechain (1, toggleEvery);

    int toggleCount = 0;

    for (int position = 0; position + toggleEvery <= totalSamples; position += toggleEvery)
    {
        engine.setBandSidechainExternal (bandIndex, (toggleCount % 2) == 1);
        engine.setBandDetectorWide (bandIndex, ((toggleCount / 2) % 2) == 1);
        ++toggleCount;

        TestHelpers::fillWithSine (main, testSampleRate, centreFrequencyHz, amplitude, position);
        // A deliberately different sidechain tone and level, so a switch
        // really does change what the detector hears.
        TestHelpers::fillWithSine (sidechain, testSampleRate, 3000.0, 0.35f, position);

        juce::dsp::AudioBlock<float> mainBlock (main);
        const juce::dsp::AudioBlock<const float> sidechainBlock (sidechain);
        engine.process (mainBlock, sidechainBlock);

        output.copyFrom (0, position, main, 0, 0, toggleEvery);
    }

    REQUIRE (TestHelpers::allSamplesFinite (output));
    CHECK (TestHelpers::maxSampleToSampleJump (output) < maxAllowedJump);
}

TEST_CASE ("Listen auditions the actual detector feed in every SC Source/Mode combination", "[dsp][sidechain][listen]")
{
    // F4's Listen contract: band-passed pre-chain audio in Split, full-range
    // in Wide, the sidechain signal when SC Source is External. Measured by
    // how much of an out-of-band main tone survives into the listen output.
    constexpr float amplitude = 0.5f;
    constexpr double outOfBandHz = 4.0 * centreFrequencyHz;

    const auto measureListenRms = [] (bool external, bool wide, double mainHz, float mainAmplitude)
    {
        LancetEngine engine;
        configureCuttingBand (engine, -80.0f); // irrelevant here; Listen is independent of the gain path
        engine.setBandListen (bandIndex, true);
        engine.setBandSidechainExternal (bandIndex, external);
        engine.setBandDetectorWide (bandIndex, wide);
        engine.prepare (makeSpec());

        juce::AudioBuffer<float> main (1, blockSamples);
        juce::AudioBuffer<float> sidechain (1, blockSamples);
        double rms = 0.0;

        for (int block = 0; block < 20; ++block)
        {
            const auto offset = static_cast<juce::int64> (block) * blockSamples;
            TestHelpers::fillWithSine (main, testSampleRate, mainHz, mainAmplitude, offset);
            // Sidechain deliberately silent, so "did the listen output follow
            // the sidechain" is answerable by level alone.
            sidechain.clear();

            juce::dsp::AudioBlock<float> mainBlock (main);
            const juce::dsp::AudioBlock<const float> sidechainBlock (sidechain);
            engine.process (mainBlock, sidechainBlock);

            rms = TestHelpers::rms (main);
        }

        return rms;
    };

    const auto splitInternal = measureListenRms (false, false, outOfBandHz, amplitude);
    const auto wideInternal = measureListenRms (false, true, outOfBandHz, amplitude);
    const auto splitExternal = measureListenRms (true, false, outOfBandHz, amplitude);

    INFO ("Split/Internal listen RMS = " << splitInternal
          << ", Wide/Internal = " << wideInternal
          << ", Split/External = " << splitExternal);

    // Wide passes the out-of-band tone through untouched; Split rejects it.
    CHECK (wideInternal > splitInternal * 4.0);
    // External auditions the (silent) sidechain, not the main programme.
    CHECK (splitExternal < 1.0e-4);
}

//==============================================================================
// Processor-level plumbing: the same routing through the real sidechain bus.

TEST_CASE ("Processor: an enabled sidechain bus drives an External band while the main input stays quiet",
           "[dsp][sidechain][processor]")
{
    LancetAudioProcessor processor;

    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add (juce::AudioChannelSet::stereo());
    layout.inputBuses.add (juce::AudioChannelSet::stereo());
    layout.outputBuses.add (juce::AudioChannelSet::stereo());
    REQUIRE (processor.setBusesLayout (layout));

    const auto setParam = [&processor] (const char* id, float realValue)
    {
        auto* param = processor.apvts.getParameter (id);
        REQUIRE (param != nullptr);
        param->setValueNotifyingHost (param->convertTo0to1 (realValue));
    };

    const auto setNormalised = [&processor] (const char* id, float normalised)
    {
        auto* param = processor.apvts.getParameter (id);
        REQUIRE (param != nullptr);
        param->setValueNotifyingHost (normalised);
    };

    setNormalised (ParamIDs::b3On, 1.0f);
    setParam (ParamIDs::b3Freq, centreFrequencyHz);
    setParam (ParamIDs::b3Q, 1.0f);
    setParam (ParamIDs::b3Range, -12.0f);
    setParam (ParamIDs::b3Threshold, -20.0f);
    setParam (ParamIDs::b3Attack, 1.0f);
    setParam (ParamIDs::b3Release, 50.0f);
    setNormalised (ParamIDs::b3ScSource, 1.0f); // External

    processor.prepareToPlay (testSampleRate, blockSamples);

    juce::AudioBuffer<float> buffer (4, blockSamples); // 2 main + 2 sidechain
    juce::MidiBuffer midi;

    double lastMainRms = 0.0;

    for (int block = 0; block < 60; ++block)
    {
        const auto offset = static_cast<juce::int64> (block) * blockSamples;

        for (int channel = 0; channel < 2; ++channel)
        {
            auto* main = buffer.getWritePointer (channel);
            auto* sidechain = buffer.getWritePointer (channel + 2);

            for (int i = 0; i < blockSamples; ++i)
            {
                const auto phase = juce::MathConstants<double>::twoPi * centreFrequencyHz
                                    * static_cast<double> (offset + i) / testSampleRate;
                main[i] = 0.3f * static_cast<float> (std::sin (phase));
                sidechain[i] = 0.9f * static_cast<float> (std::sin (phase));
            }
        }

        processor.processBlock (buffer, midi);

        double sum = 0.0;

        for (int i = 0; i < blockSamples; ++i)
            sum += static_cast<double> (buffer.getSample (0, i)) * static_cast<double> (buffer.getSample (0, i));

        lastMainRms = std::sqrt (sum / blockSamples);
    }

    // The main tone is -10.5 dBFS, comfortably above the -20 dB threshold on
    // its own, but the sidechain is 9.5 dB hotter - so the realised cut has to
    // be deeper than what the main signal alone would produce. Comparing
    // against the un-processed 0.3-amplitude tone's own RMS (0.3/sqrt(2)) is
    // enough to show a real, large cut.
    const auto unprocessedRms = 0.3 / std::sqrt (2.0);
    const auto measuredCutDb = juce::Decibels::gainToDecibels (lastMainRms / unprocessedRms);

    INFO ("measured cut with external sidechain = " << measuredCutDb << " dB");
    CHECK (std::isfinite (lastMainRms));
    CHECK (measuredCutDb < -8.0);
    CHECK (processor.getLatencySamples() == 0);
}

#include "AllocationGuard.h"
#include "PluginProcessor.h"
#include "dsp/LancetEngine.h"
#include "params/ParameterIds.h"
#include "TestHelpers.h"

#include <catch2/catch_test_macros.hpp>

#include <array>

// Permanent audio-thread allocation regression guard, added for the v0.2.0
// deep-dive pass (docs/design-brief.md). Neither pluginval nor auval do
// allocation-instrumented profiling, and this repo had no allocation-
// counting mechanism before v0.2.0, so this test file establishes the
// baseline AND extends coverage to the two new v0.2.0 code paths
// specifically: Detector's auto-release measurement/second envelope
// (Detector.cpp) and DynamicBand's gain/Q-coupled Q recompute
// (DynamicBand.cpp's computeMainFilterQ()) - both are on the per-sub-block
// hot path once their respective toggle is enabled, and both were written
// using only stack-allocated arithmetic (no juce::dsp::IIR::Coefficients::
// make*() allocating calls, no containers), which this test verifies
// end-to-end rather than by code inspection alone.
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
}

TEST_CASE ("LancetAudioProcessor::processBlock allocates no memory with every band engaged and "
           "AutoRelease/GainQ on, while parameters keep moving",
           "[dsp][rt-safety][alloc]")
{
    LancetAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    // Touch every parameter this test moves at least once before the guard
    // starts - a parameter's very first setValueNotifyingHost() call can
    // lazily warm up internal JUCE bookkeeping (observed as a one-time
    // allocation in sibling plugins' own AllocationTests.cpp), which must
    // happen outside the guarded region.
    for (int band = 1; band <= LancetEngine::numBands; ++band)
    {
        const auto prefix = "b" + juce::String (band) + "_";
        setParamNormalised (processor, (prefix + "on").toRawUTF8(), 1.0f);
        setParam (processor, (prefix + "freq").toRawUTF8(), 1000.0f);
        setParam (processor, (prefix + "q").toRawUTF8(), 1.0f);
        setParam (processor, (prefix + "gain").toRawUTF8(), 0.0f);
        setParam (processor, (prefix + "range").toRawUTF8(), -9.0f);
        setParam (processor, (prefix + "thresh").toRawUTF8(), -30.0f);
        setParam (processor, (prefix + "attack").toRawUTF8(), 3.0f);
        setParam (processor, (prefix + "release").toRawUTF8(), 120.0f);
        setParamNormalised (processor, (prefix + "autoRelease").toRawUTF8(), 1.0f);
        setParamNormalised (processor, (prefix + "gainQ").toRawUTF8(), 1.0f);
    }

    // Resolve every parameter pointer touched inside the guarded loop below
    // ONCE, here, outside the guard - juce::String concatenation (building
    // "b1_freq" etc. fresh every iteration) itself heap-allocates, which
    // would corrupt this test's measurement of the *plugin's* own
    // allocation behaviour with the *test harness's* own string-building
    // allocations. Precomputing raw juce::RangedAudioParameter* pointers
    // avoids that entirely, matching how PluginProcessor.cpp's own
    // pushParametersToEngine() resolves parameters once at construction
    // time rather than by ID lookup every block.
    std::array<std::array<juce::RangedAudioParameter*, 4>, LancetEngine::numBands> movingParams {};

    for (int band = 1; band <= LancetEngine::numBands; ++band)
    {
        const auto prefix = "b" + juce::String (band) + "_";
        auto& params = movingParams[static_cast<size_t> (band - 1)];
        params[0] = processor.apvts.getParameter ((prefix + "freq").toRawUTF8());
        params[1] = processor.apvts.getParameter ((prefix + "range").toRawUTF8());
        params[2] = processor.apvts.getParameter ((prefix + "attack").toRawUTF8());
        params[3] = processor.apvts.getParameter ((prefix + "release").toRawUTF8());

        for (auto* param : params)
            REQUIRE (param != nullptr);
    }

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;

    // Allocation during prepareToPlay()/parameter smoothing settle is
    // expected and allowed - only the steady-state per-block behaviour
    // below is guarded.
    for (int warmup = 0; warmup < 4; ++warmup)
    {
        TestHelpers::fillWithSine (buffer, 48000.0, 1000.0, 0.7f, static_cast<juce::int64> (warmup) * 512);
        processor.processBlock (buffer, midi);
    }

    TestAlloc::AllocationGuard guard;

    for (int block = 0; block < 32; ++block)
    {
        // Keep every band's Freq/Range/Attack/Release moving every block -
        // both the main filter's and the Detector's coefficient recomputes
        // (once per 32-sample sub-block, see LancetEngine.cpp) and the
        // auto-release/gain-Q-coupling arithmetic run continuously as a
        // result, exactly the steady-state condition that would surface a
        // hidden per-block allocation.
        const auto sweep = static_cast<float> (block) / 32.0f;

        for (auto& params : movingParams)
        {
            params[0]->setValueNotifyingHost (params[0]->convertTo0to1 (200.0f + sweep * 8000.0f));
            params[1]->setValueNotifyingHost (params[1]->convertTo0to1 (-12.0f + sweep * 24.0f));
            params[2]->setValueNotifyingHost (params[2]->convertTo0to1 (0.1f + sweep * 100.0f));
            params[3]->setValueNotifyingHost (params[3]->convertTo0to1 (5.0f + sweep * 500.0f));
        }

        TestHelpers::fillWithSine (buffer, 48000.0, 1000.0, 0.7f, static_cast<juce::int64> (block) * 512);
        processor.processBlock (buffer, midi);
    }

    CHECK (guard.count() == 0);
}

TEST_CASE ("LancetEngine::process allocates no memory across repeated blocks with AutoRelease/GainQ on",
           "[dsp][engine][rt-safety][alloc]")
{
    // Isolated from PluginProcessor/APVTS so this attributes any regression
    // specifically to LancetEngine/DynamicBand/Detector's own coefficient
    // and auto-release recompute, independent of the processor's parameter
    // plumbing.
    LancetEngine engine;

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = 48000.0;
    spec.maximumBlockSize = 512;
    spec.numChannels = 2;
    engine.prepare (spec);

    for (int band = 0; band < LancetEngine::numBands; ++band)
    {
        engine.setBandOn (band, true);
        engine.setBandFrequencyHz (band, 1000.0f);
        engine.setBandQ (band, 1.0f);
        engine.setBandGainDb (band, 0.0f);
        engine.setBandRangeDb (band, -9.0f);
        engine.setBandThresholdDb (band, -30.0f);
        engine.setBandAttackMs (band, 3.0f);
        engine.setBandReleaseMs (band, 120.0f);
        engine.setBandAutoRelease (band, true);
        engine.setBandGainQ (band, true);
    }

    juce::AudioBuffer<float> buffer (2, 512);
    TestHelpers::fillWithSine (buffer, 48000.0, 1000.0, 0.7f);

    juce::dsp::AudioBlock<float> block (buffer);

    // Warm-up block outside the guard, as above.
    engine.process (block);

    TestAlloc::AllocationGuard guard;

    for (int i = 0; i < 32; ++i)
    {
        const auto sweep = static_cast<float> (i) / 32.0f;

        for (int band = 0; band < LancetEngine::numBands; ++band)
        {
            engine.setBandFrequencyHz (band, 200.0f + sweep * 8000.0f);
            engine.setBandRangeDb (band, -12.0f + sweep * 24.0f);
            engine.setBandAttackMs (band, 0.1f + sweep * 100.0f);
            engine.setBandReleaseMs (band, 5.0f + sweep * 500.0f);
        }

        TestHelpers::fillWithSine (buffer, 48000.0, 1000.0, 0.7f, static_cast<juce::int64> (i) * 512);
        engine.process (block);
    }

    CHECK (guard.count() == 0);
}

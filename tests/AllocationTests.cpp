#include "AllocationGuard.h"
#include "PluginProcessor.h"
#include "dsp/LancetEngine.h"
#include "params/ParameterIds.h"
#include "TestHelpers.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <new>

//==============================================================================
// Suite-wide hardening wave (2026-07-31): the guard mechanism itself was
// never self-verified anywhere in this file (or elsewhere in the suite) -
// every TEST_CASE below only ever checks `guard.count() == 0`, which would
// read exactly the same whether the guard is working correctly OR is
// silently broken (e.g. by a future edit to AllocationGuard.cpp that stops
// routing through the replaced operator new). This closes that gap.
//
// The canary allocation deliberately goes through a direct call to
// ::operator new, not a `new`/`delete` expression. [expr.new] explicitly
// permits an implementation to omit the allocation of a new-expression
// whose storage is never observably used, and Clang/MSVC do exactly that at
// higher optimisation levels - the exact defect class already found and
// fixed in sibling plugins (Requiem's tests/EngineTests.cpp:481). A direct
// ::operator new call is a plain function call, so that elision permission
// does not apply, and the volatile write forces the returned storage to be
// observably used. It is also written outside any Catch2 assertion macro:
// wrapping it in CHECK()/REQUIRE() risks the macro's own internal
// allocations polluting the count, which would make a passing self-check
// prove nothing about the canary specifically.
TEST_CASE ("TestAlloc::AllocationGuard: the guard itself fires on ordinary heap allocations "
           "and stays silent on pure stack/register arithmetic",
           "[dsp][rt-safety][alloc][self-test]")
{
    // Both guarded regions below contain ONLY the raw allocation/arithmetic
    // - no CHECK()/REQUIRE() call runs while the guard is still armed, since
    // Catch2's own assertion macros are not guaranteed allocation-free (see
    // the file comment above) and would corrupt the very count they are
    // meant to verify. The guard's count() is a static read of a counter
    // that outlives the guard's own destruction, so asserting on it after
    // the scope closes is exactly as accurate and does not have this
    // problem.
    std::size_t countAfterAllocation = 0;

    {
        TestAlloc::AllocationGuard guard;

        auto* deliberate = static_cast<int*> (::operator new (sizeof (int)));
        *static_cast<volatile int*> (deliberate) = 7;
        ::operator delete (deliberate);

        countAfterAllocation = guard.count();
    }

    CHECK (countAfterAllocation >= 1);

    std::size_t countAfterArithmetic = 0;

    {
        TestAlloc::AllocationGuard guard;
        volatile auto sum = 0.0f;

        for (int i = 0; i < 1000; ++i)
            sum = sum + static_cast<float> (i);

        countAfterArithmetic = guard.count();
    }

    CHECK (countAfterArithmetic == 0);
}

// Permanent audio-thread allocation regression guard, added for the v0.2.0
// deep-dive pass (docs/design-brief.md). Neither pluginval nor auval do
// allocation-instrumented profiling, and this repo had no allocation-
// counting mechanism before v0.2.0, so this test file establishes the
// baseline AND extends coverage to the v0.2.0/v0.3.0 code paths
// specifically: Detector's auto-release measurement/second envelope
// (Detector.cpp), DynamicBand's gain/Q-coupled Q recompute
// (DynamicBand.cpp's computeMainFilterQ()), and (v0.3.0,
// docs/voicing-notes.md) DynamicBand's saturation waveshaper
// (computeSaturationDrive()/applySaturation(), std::tanh only, no
// allocation) - all three are on the per-sub-block hot path once their
// respective toggle is enabled, and all were written using only
// stack-allocated arithmetic (no juce::dsp::IIR::Coefficients::make*()
// allocating calls, no containers), which this test verifies end-to-end
// rather than by code inspection alone. Range is kept positive (boosting)
// here specifically so the saturation branch (only taken while a band is
// actively boosting) actually runs, not just AutoRelease/GainQ's own paths.
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
           "AutoRelease/GainQ/Saturation on, while parameters keep moving",
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
        // Positive (boosting) Range - required so DynamicBand's saturation
        // branch (only taken while appliedGainDb > 0) actually runs below.
        setParam (processor, (prefix + "range").toRawUTF8(), 9.0f);
        setParam (processor, (prefix + "thresh").toRawUTF8(), -30.0f);
        setParam (processor, (prefix + "attack").toRawUTF8(), 3.0f);
        setParam (processor, (prefix + "release").toRawUTF8(), 120.0f);
        setParamNormalised (processor, (prefix + "autoRelease").toRawUTF8(), 1.0f);
        setParamNormalised (processor, (prefix + "gainQ").toRawUTF8(), 1.0f);
        setParamNormalised (processor, (prefix + "sat").toRawUTF8(), 1.0f);
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

TEST_CASE ("LancetEngine::process allocates no memory across repeated blocks with AutoRelease/GainQ/Saturation on",
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
        // Positive (boosting) Range - required so DynamicBand's saturation
        // branch (only taken while appliedGainDb > 0) actually runs below.
        engine.setBandRangeDb (band, 9.0f);
        engine.setBandThresholdDb (band, -30.0f);
        engine.setBandAttackMs (band, 3.0f);
        engine.setBandReleaseMs (band, 120.0f);
        engine.setBandAutoRelease (band, true);
        engine.setBandGainQ (band, true);
        engine.setBandSaturation (band, true);
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

//==============================================================================
// v0.4.0 (SOTA brief T11): the same guard, extended to everything this
// release added to the audio thread - the sidechain bus slicing in
// processBlock (which wraps channel pointers rather than copying, and must
// not be doing anything cleverer than that), the per-sample SVF coefficient
// path, the ADAA saturator, and the two new per-band choice parameters being
// toggled continuously.
TEST_CASE ("LancetAudioProcessor::processBlock allocates no memory with the sidechain bus active and the "
           "v0.4.0 detector routing toggling every block",
           "[dsp][rt-safety][alloc][sidechain]")
{
    LancetAudioProcessor processor;

    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add (juce::AudioChannelSet::stereo());
    layout.inputBuses.add (juce::AudioChannelSet::stereo());
    layout.outputBuses.add (juce::AudioChannelSet::stereo());
    REQUIRE (processor.setBusesLayout (layout));

    processor.prepareToPlay (48000.0, 512);

    for (int band = 1; band <= LancetEngine::numBands; ++band)
    {
        const auto prefix = "b" + juce::String (band) + "_";
        setParamNormalised (processor, (prefix + "on").toRawUTF8(), 1.0f);
        setParam (processor, (prefix + "freq").toRawUTF8(), 1000.0f);
        setParam (processor, (prefix + "q").toRawUTF8(), 1.0f);
        setParam (processor, (prefix + "gain").toRawUTF8(), 0.0f);
        setParam (processor, (prefix + "range").toRawUTF8(), 9.0f);
        setParam (processor, (prefix + "thresh").toRawUTF8(), -30.0f);
        setParam (processor, (prefix + "attack").toRawUTF8(), 3.0f);
        setParam (processor, (prefix + "release").toRawUTF8(), 120.0f);
        setParamNormalised (processor, (prefix + "autoRelease").toRawUTF8(), 1.0f);
        setParamNormalised (processor, (prefix + "gainQ").toRawUTF8(), 1.0f);
        setParamNormalised (processor, (prefix + "sat").toRawUTF8(), 1.0f);
        // Both new choices touched once here, outside the guard, so their own
        // first-use bookkeeping cannot be mistaken for a per-block allocation.
        setParamNormalised (processor, (prefix + "scSource").toRawUTF8(), 1.0f);
        setParamNormalised (processor, (prefix + "scMode").toRawUTF8(), 1.0f);
        setParamNormalised (processor, (prefix + "scSource").toRawUTF8(), 0.0f);
        setParamNormalised (processor, (prefix + "scMode").toRawUTF8(), 0.0f);
    }

    // Resolve every pointer used inside the guarded loop up front - building
    // "b1_scSource" fresh each iteration would allocate in the *test*.
    std::array<std::array<juce::RangedAudioParameter*, 2>, LancetEngine::numBands> routingParams {};

    for (int band = 1; band <= LancetEngine::numBands; ++band)
    {
        const auto prefix = "b" + juce::String (band) + "_";
        auto& params = routingParams[static_cast<size_t> (band - 1)];
        params[0] = processor.apvts.getParameter ((prefix + "scSource").toRawUTF8());
        params[1] = processor.apvts.getParameter ((prefix + "scMode").toRawUTF8());

        for (auto* param : params)
            REQUIRE (param != nullptr);
    }

    juce::AudioBuffer<float> buffer (4, 512); // 2 main + 2 sidechain
    juce::MidiBuffer midi;

    for (int warmup = 0; warmup < 4; ++warmup)
    {
        buffer.clear();
        TestHelpers::fillWithSine (buffer, 48000.0, 1000.0, 0.7f, static_cast<juce::int64> (warmup) * 512);
        processor.processBlock (buffer, midi);
    }

    TestAlloc::AllocationGuard guard;

    for (int block = 0; block < 32; ++block)
    {
        // Both routing switches flipping every block - the Wide -> Split
        // transition re-primes the detector bandpass, which is the one v0.4.0
        // code path that touches filter state outside prepare().
        for (auto& params : routingParams)
        {
            params[0]->setValueNotifyingHost ((block % 2) == 0 ? 1.0f : 0.0f);
            params[1]->setValueNotifyingHost ((block % 4) < 2 ? 1.0f : 0.0f);
        }

        TestHelpers::fillWithSine (buffer, 48000.0, 1000.0, 0.7f, static_cast<juce::int64> (block) * 512);
        processor.processBlock (buffer, midi);
    }

    CHECK (guard.count() == 0);
}

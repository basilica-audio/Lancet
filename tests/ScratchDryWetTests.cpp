#include "dsp/LancetEngine.h"
#include "TestHelpers.h"

#include <catch2/catch_test_macros.hpp>

// Regression coverage for a real bug found while writing the M1 test suite:
// juce::dsp::Gain<float>'s internal SmoothedValue default-constructs its
// target at *linear* 0 (silence), not unity/0 dB (see juce_Gain.h,
// JUCE 8.0.14 - SmoothedValue's own default constructor initialises Linear-
// type smoothers to 0). A LancetEngine that is prepare()'d without ever
// having setInputTrimDb()/setOutputTrimDb() called first would otherwise
// stay completely silent - LancetAudioProcessor always calls both (via
// pushParametersToEngine()) before prepare(), so the shipped plugin was
// never actually affected, but LancetEngine's own public API needs to
// default safely for direct use (as these tests do). Fixed in
// LancetEngine::prepare() by re-priming inputTrim/outputTrim from
// lastInputTrimDb/lastOutputTrimDb (both defaulting to 0 dB) after
// juce::dsp::Gain::prepare() - see LancetEngine.h/.cpp.
//
// Writing tests/NullTests.cpp's "on with Gain=0/Range=0" guarantee also
// surfaced a second, unrelated precision finding along the way: see
// RealtimeCoefficients.h's applyBiquadCoefficients() (an x/x-vs-x*(1/x)
// normalisation fix) and DynamicBand.cpp's exact-0-dB bypass (a compiler
// FMA-contraction-dependent TDF-II precision limit that coefficient math
// alone cannot close).
TEST_CASE ("LancetEngine: Input/Output Trim default to unity (0 dB), not silence, without an explicit setter call",
           "[dsp][engine][regression]")
{
    LancetEngine engine;
    // Deliberately no setInputTrimDb()/setOutputTrimDb() call before
    // prepare() - this is the exact scenario that exposed the bug.

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = 48000.0;
    spec.maximumBlockSize = 512;
    spec.numChannels = 1;
    engine.prepare (spec);

    juce::AudioBuffer<float> buffer (1, 512);
    TestHelpers::fillWithSine (buffer, 48000.0, 1000.0, 0.5f);

    juce::dsp::AudioBlock<float> block (buffer);
    engine.process (block);

    CHECK (TestHelpers::peakAbsolute (buffer) > 0.1f);
}

TEST_CASE ("LancetEngine: Input/Output Trim still default to unity after a sample-rate change (re-prepare)",
           "[dsp][engine][regression]")
{
    LancetEngine engine;

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = 44100.0;
    spec.maximumBlockSize = 512;
    spec.numChannels = 1;
    engine.prepare (spec);

    // Re-prepare at a different sample rate without ever touching trim -
    // guards the same defaulting behaviour surviving a prepareToPlay()
    // called again mid-session.
    spec.sampleRate = 96000.0;
    engine.prepare (spec);

    juce::AudioBuffer<float> buffer (1, 512);
    TestHelpers::fillWithSine (buffer, 96000.0, 1000.0, 0.5f);

    juce::dsp::AudioBlock<float> block (buffer);
    engine.process (block);

    CHECK (TestHelpers::peakAbsolute (buffer) > 0.1f);
}

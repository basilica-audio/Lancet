#pragma once

#include "DynamicBand.h"

#include <array>

// The complete Lancet signal path, independent of juce::AudioProcessor so
// it can be exercised directly by unit tests without instantiating a full
// plugin (see tests/EngineTests.cpp). Owns all DSP state; every buffer/
// filter is allocated in prepare() and never reallocated on the audio
// thread.
//
// Signal flow (see docs/architecture.md for the full diagram):
//
//   in --[Input Trim]--+--[pre-chain tap]--> each band's Detector (see below)
//                       |
//                       +--> Band1 -> Band2 -> Band3 -> Band4 -> Band5 -> Band6 --> [Mix] --> [Output Trim] --> out
//
// Bands process serially, standard parametric-EQ style; each band's own
// Detector (see Detector.h/DynamicBand.h) taps the *pre-chain* signal (right
// after Input Trim, before Band 1), not that band's own serially-processed
// input, so a downstream band's gain move never perturbs an upstream band's
// (or its own) detection - every band always "sees" the same input.
//
// Mix (see ParamIDs::mix) is a parallel dry/wet blend of the whole six-band
// chain via juce::dsp::DryWetMixer, with "dry" tapped right after Input Trim
// (i.e. Input Trim always applies to both the dry and wet paths equally,
// while Mix controls how much of the *EQ's effect* reaches the output) and
// Output Trim applied after the blend, so it consistently shapes overall
// level regardless of the Mix setting.
//
// "Listen" (see ParamIDs::b1Listen etc.) is an exclusive per-band sidechain
// solo: engaging it on band N replaces the program output with that band's
// own bandpass-filtered detector signal (see Detector::getListenBuffer()),
// for auditioning exactly what triggers that band's dynamic move. All six
// bands keep processing normally underneath regardless (their own envelopes
// stay warm), so re-disabling Listen never pops.
//
// Every filter in this chain (the six bands' bell/shelf filters and their
// Detectors' bandpass filters) is a minimum-phase IIR biquad with no
// lookahead, so LancetEngine adds zero latency - see getLatencySamples().
class LancetEngine
{
public:
    LancetEngine();

    // Allocates all DSP state. Must be called (and completed) before the
    // first process() call, and again whenever sample rate/block size/
    // channel count change.
    void prepare (const juce::dsp::ProcessSpec& spec);

    // Clears all filter/envelope/gain-ramp state without deallocating.
    // Safe to call from the audio thread (e.g. on playback stop/loop).
    void reset();

    // Processes `block` in place. Real-time safe: no allocation once
    // prepare() has completed. A zero-sample block is a safe no-op; a block
    // larger than what prepare() was sized for is defensively trimmed to
    // the prepared capacity rather than causing an out-of-bounds write.
    void process (juce::dsp::AudioBlock<float>& block) noexcept;

    static constexpr int numBands = 6;

    // Per-band parameter setters, in real units (Hz, dB, ms). `bandIndex`
    // is 0-based (0 = Band 1 ... 5 = Band 6). Safe to call every block from
    // the audio thread - see DynamicBand.h for what each does.
    void setBandOn (int bandIndex, bool isOn) noexcept;
    void setBandShelfSelected (int bandIndex, bool useShelf) noexcept;
    void setBandFrequencyHz (int bandIndex, float frequencyHz) noexcept;
    void setBandQ (int bandIndex, float q) noexcept;
    void setBandGainDb (int bandIndex, float gainDb) noexcept;
    void setBandRangeDb (int bandIndex, float rangeDb) noexcept;
    void setBandThresholdDb (int bandIndex, float thresholdDb) noexcept;
    void setBandAttackMs (int bandIndex, float attackMs) noexcept;
    void setBandReleaseMs (int bandIndex, float releaseMs) noexcept;
    void setBandListen (int bandIndex, bool listen) noexcept;

    // Global trim/mix.
    void setInputTrimDb (float newTrimDb) noexcept;
    void setOutputTrimDb (float newTrimDb) noexcept;
    void setMixPercent (float newMixPercent) noexcept;

    const DynamicBand& getBand (int bandIndex) const noexcept { return *bands[static_cast<size_t> (bandIndex)]; }

    // Always 0: every filter in this engine (bell/shelf bands, Detector
    // bandpasses) is minimum-phase with no lookahead - see class comment.
    static constexpr int getLatencySamples() noexcept { return 0; }

private:
    static constexpr double smoothingTimeSeconds = 0.05;

    // Coefficient/gain updates happen once per sub-block of at most this
    // many samples, per docs/design-brief.md ("Coefficient update per
    // 32-sample sub-block with smoothed gain").
    static constexpr size_t subBlockSize = 32;

    double sampleRate = 44100.0;

    juce::dsp::Gain<float> inputTrim;
    juce::dsp::Gain<float> outputTrim;
    juce::dsp::DryWetMixer<float> dryWetMixer;

    // Last commanded mix proportion (0-1), re-primed on every prepare() -
    // see the DryWetMixer priming gotcha noted in LancetEngine.cpp.
    float lastMixProportion = 1.0f;

    // Last commanded trim values in dB, re-primed on every prepare() - see
    // LancetEngine.cpp's prepare() for why: juce::dsp::Gain<float>'s own
    // internal SmoothedValue default-constructs its target at *linear* 0
    // (silence), not unity/0 dB, so a freshly constructed LancetEngine that
    // is prepare()'d without ever having a setInputTrimDb()/
    // setOutputTrimDb() call first would otherwise produce total silence
    // until a parameter change happened to touch trim. LancetAudioProcessor
    // always calls setInputTrimDb()/setOutputTrimDb() (via
    // pushParametersToEngine()) before prepare(), so this never bites the
    // shipped plugin, but LancetEngine's own public API must still default
    // safely for direct use (e.g. by tests).
    float lastInputTrimDb = 0.0f;
    float lastOutputTrimDb = 0.0f;

    DynamicBand band1 { DynamicBand::ShelfDirection::low };
    DynamicBand band2 { DynamicBand::ShelfDirection::none };
    DynamicBand band3 { DynamicBand::ShelfDirection::none };
    DynamicBand band4 { DynamicBand::ShelfDirection::none };
    DynamicBand band5 { DynamicBand::ShelfDirection::none };
    DynamicBand band6 { DynamicBand::ShelfDirection::high };

    std::array<DynamicBand*, numBands> bands { &band1, &band2, &band3, &band4, &band5, &band6 };

    // Pre-chain tap: a copy of the signal right after Input Trim, before
    // Band 1 - every band's Detector reads from this (not from the
    // evolving in-place main signal), sized to the maximum block declared
    // in prepare() and never reallocated on the audio thread.
    juce::AudioBuffer<float> preChainBuffer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LancetEngine)
};

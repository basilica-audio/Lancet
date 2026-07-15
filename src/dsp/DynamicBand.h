#pragma once

#include "Detector.h"

#include <juce_dsp/juce_dsp.h>

// One dynamic-EQ band: a serial bell/shelf filter (juce::dsp::IIR::Filter
// via juce::dsp::ProcessorDuplicator, so a single instance covers mono or
// stereo) whose gain is `static gain + dynamic gain`, where the dynamic
// component is derived from a Detector (bandpass-filtered envelope of the
// plugin's pre-chain input, matched to this band's own frequency/Q - see
// Detector.h) crossed against Threshold with a soft knee and clamped to
// +-Range.
//
// Gain-computer formula (dB domain, matching the classic quadratic soft-knee
// gain computer from Giannoulis/Massberg/Reiss, "Digital Dynamic Range
// Compressor Design - A Tutorial and Analysis", JAES 2012, eq. 4):
//
//   x = levelDb - thresholdDb                       (overshoot, can be negative)
//   gc(x) = 0                                        if 2x <= -knee
//         = (x + knee/2)^2 / (2*knee)                if 2|x| < knee   (soft-knee region)
//         = x                                        if 2x >= knee
//   dynamicMagnitudeDb = clamp(gc(x), 0, |Range|)
//   dynamicGainDb = dynamicMagnitudeDb * sign(Range)
//
// i.e. gain moves 1:1 with overshoot once fully above the knee (there is no
// separate "ratio" parameter in the M1 spec - see docs/design-brief.md),
// smoothly ramped in over a 6 dB knee width around Threshold, and hard
// capped at the user's Range so it can never exceed the configured depth.
// Negative Range cuts as the signal gets louder (the classic de-esser/
// resonance-tamer use case); positive Range boosts as it gets louder
// (upward "duck-in" expansion). Range == 0 disables the dynamic term
// entirely (a pure static EQ band).
//
// Coefficient updates (both the main filter's and the Detector's bandpass)
// are real-time safe (see RealtimeCoefficients.h) and are only ever done
// once per `processSubBlock()` call - the caller (LancetEngine) is
// responsible for chunking a full block into <= 32-sample sub-blocks, per
// docs/design-brief.md ("Coefficient update per 32-sample sub-block with
// smoothed gain"). The combined static+dynamic gain is additionally run
// through a juce::SmoothedValue ramped across each sub-block before being
// baked into filter coefficients, so successive sub-block coefficient
// snapshots never jump abruptly (the zipper guard the design brief's
// guarantee #10 tests for).
class DynamicBand
{
public:
    enum class ShelfDirection
    {
        none, // Bands 2-5: always Bell, no Type parameter at all.
        low,  // Band 1: Bell or LowShelf.
        high  // Band 6: Bell or HighShelf.
    };

    explicit DynamicBand (ShelfDirection shelfDirectionToUse) noexcept;

    // Allocates all DSP state (Detector, main filter, listen buffer). Must
    // be called before the first processSubBlock() call, and again
    // whenever sample rate/block size/channel count change.
    void prepare (const juce::dsp::ProcessSpec& spec);

    // Clears filter/envelope/gain-ramp state without deallocating. Safe to
    // call from the audio thread.
    void reset();

    // Processes one <= 32-sample sub-block. `mainSubBlock` is this band's
    // slice of the serial main signal path (processed in place if the band
    // is on; left untouched otherwise - a true bypass, not just a 0 dB
    // filter, so an off band is bit-identical). `preChainBlock` is the
    // *whole* current plugin block's pre-chain tap (see LancetEngine);
    // `startSample`/`numSamples` select this call's slice of it, matching
    // `mainSubBlock`'s own extent. Real-time safe: no allocation once
    // prepare() has completed.
    void processSubBlock (juce::dsp::AudioBlock<float> mainSubBlock,
                           const juce::dsp::AudioBlock<const float>& preChainBlock,
                           size_t startSample,
                           size_t numSamples) noexcept;

    void setOn (bool shouldBeOn) noexcept { on = shouldBeOn; }
    void setShelfSelected (bool shouldUseShelf) noexcept { shelfSelected = shouldUseShelf; }
    void setFrequencyHz (float newFrequencyHz) noexcept { frequencyHz = newFrequencyHz; }
    void setQ (float newQ) noexcept { q = newQ; }
    void setStaticGainDb (float newGainDb) noexcept { staticGainDb = newGainDb; }
    void setRangeDb (float newRangeDb) noexcept { rangeDb = newRangeDb; }
    void setThresholdDb (float newThresholdDb) noexcept { thresholdDb = newThresholdDb; }
    void setAttackMs (float newAttackMs) noexcept { detector.setAttackMs (newAttackMs); }
    void setReleaseMs (float newReleaseMs) noexcept { detector.setReleaseMs (newReleaseMs); }
    void setListen (bool shouldListen) noexcept { listen = shouldListen; }

    bool isListening() const noexcept { return listen; }
    bool isOn() const noexcept { return on; }

    // The band's own bandpass-filtered detector signal, one full plugin
    // block's worth - used to substitute the final output when this band's
    // Listen is engaged (see LancetEngine::process()).
    const juce::AudioBuffer<float>& getListenBuffer() const noexcept { return detector.getListenBuffer(); }

    float getLastDetectorLevelDb() const noexcept { return detector.getLastLevelDb(); }

private:
    // Standard "flat"/Butterworth shelf slope (Q = 1/sqrt(2)), matching the
    // implicit default juce::dsp::IIR::ArrayCoefficients::makeLowShelf/
    // makeHighShelf use when no explicit Q is given. Q is documented as
    // "ignored in shelf mode" in docs/design-brief.md's parameter table;
    // this applies both to the main filter's shape and to the Detector's
    // matched bandpass (see Detector.h/.cpp usage below).
    static constexpr float fixedShelfQ = 0.70710678f;

    // Soft-knee width in dB, centred on Threshold - see class comment.
    static constexpr float kneeWidthDb = 6.0f;

    static constexpr double smoothingTimeSeconds = 0.05;

    bool isEffectivelyShelf() const noexcept { return shelfDirection != ShelfDirection::none && shelfSelected; }
    float effectiveQ() const noexcept { return isEffectivelyShelf() ? fixedShelfQ : q; }

    // Soft-knee gain-computer overshoot (see class comment), always >= 0.
    static float softKneeOvershoot (float overshootDb) noexcept;

    void updateFilterCoefficients (float appliedGainDb) noexcept;

    ShelfDirection shelfDirection;

    using Duplicator = juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>>;
    Duplicator mainFilter { new juce::dsp::IIR::Coefficients<float> (1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f) };

    Detector detector;

    double sampleRate = 44100.0;

    bool on = false;
    bool shelfSelected = false;
    bool listen = false;

    float frequencyHz = 1000.0f;
    float q = 1.0f;
    float staticGainDb = 0.0f;
    float rangeDb = 0.0f;
    float thresholdDb = -30.0f;

    // Combined static+dynamic gain, smoothed across each sub-block so
    // successive coefficient snapshots never jump abruptly (zipper guard).
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> gainSmoothed;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DynamicBand)
};

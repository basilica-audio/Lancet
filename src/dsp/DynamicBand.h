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
// smoothly ramped in over a knee width derived from Range (v0.2.0, see
// computeKneeWidthDb() below - a flat 6 dB constant pre-v0.2.0), and hard
// capped at the user's Range so it can never exceed the configured depth.
// Negative Range cuts as the signal gets louder (the classic de-esser/
// resonance-tamer use case); positive Range boosts as it gets louder
// (upward "duck-in" expansion). Range == 0 disables the dynamic term
// entirely (a pure static EQ band).
//
// v0.2.0 (docs/design-brief.md §2/§3) adds two opt-in, per-band booleans,
// both off by default (exact v0.1.0 behaviour reproduced when off):
//   - `autoRelease`: forwarded straight to Detector::setAutoRelease() - see
//     that class's docs for the full program-dependent-release mechanism.
//   - `gainQ`: widens (reduces) the *main filter's own* effective Q
//     proportionally to how far the band's current dynamic gain sits toward
//     its Range ceiling, following Sonnox Oxford Dynamic EQ's documented
//     "Q reduces with gain" analog-style softening. Deliberately scoped to
//     the main filter's coefficients only (computeMainFilterQ() below) and
//     NOT applied to the Detector's own bandpass Q (effectiveQ(), used for
//     sidechain matching) - coupling the detector's own selectivity to the
//     gain it itself produces would be a feedback loop (wider detector
//     bandpass -> different measured level -> different gain -> different
//     bandpass...); only the audible filter shape softens.
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
    void setAutoRelease (bool shouldAutoRelease) noexcept { detector.setAutoRelease (shouldAutoRelease); }
    void setGainQ (bool shouldCoupleGainToQ) noexcept { gainQEnabled = shouldCoupleGainToQ; }

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

    static constexpr double smoothingTimeSeconds = 0.05;

    // Knee-width bounds (v0.2.0, docs/design-brief.md §3): floored at 2 dB
    // (still audibly soft even for the smallest engaged Range) and clamped
    // at 10 dB (deliberately unreachable headroom given this plugin's own
    // +-12 dB Range ceiling - 12 * 0.5 = 6 dB is the real, reachable
    // maximum, matching v0.1.0's old flat 6 dB constant bit-for-bit in
    // shape at Range = +-12 dB - see test guarantee #2).
    static constexpr float kneeWidthFloorDb = 2.0f;
    static constexpr float kneeWidthCeilingDb = 10.0f;
    static constexpr float kneeWidthRangeSlope = 0.5f;

    // Gain/Q coupling bounds (v0.2.0, docs/design-brief.md §2/§3): at full
    // dynamic gain (|dynamicGainDb| == |Range|), the main filter's Q is
    // multiplied by this floor - i.e. widened to ~2.5x its nominal
    // bandwidth - a deliberately non-trivial, easily measurable softening
    // (test guarantee #4), not a subtle tweak.
    static constexpr float gainQMinMultiplier = 0.4f;

    bool isEffectivelyShelf() const noexcept { return shelfDirection != ShelfDirection::none && shelfSelected; }
    float effectiveQ() const noexcept { return isEffectivelyShelf() ? fixedShelfQ : q; }

    // The Q actually baked into the main filter's coefficients: effectiveQ()
    // above, optionally narrowed toward gainQMinMultiplier as
    // `dynamicGainDbAbs` approaches |Range| - see class comment's "gainQ"
    // paragraph for why this is scoped away from the Detector's own
    // bandpass Q.
    float computeMainFilterQ (float dynamicGainDbAbs) const noexcept;

    // Knee width in dB for the current Range setting - see class comment
    // and kneeWidthFloorDb/kneeWidthCeilingDb/kneeWidthRangeSlope above.
    float computeKneeWidthDb() const noexcept;

    // Soft-knee gain-computer overshoot (see class comment), always >= 0.
    static float softKneeOvershoot (float overshootDb, float kneeWidthDb) noexcept;

    void updateFilterCoefficients (float appliedGainDb, float mainFilterQ) noexcept;

    ShelfDirection shelfDirection;

    using Duplicator = juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>>;
    Duplicator mainFilter { new juce::dsp::IIR::Coefficients<float> (1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f) };

    Detector detector;

    double sampleRate = 44100.0;

    bool on = false;
    bool shelfSelected = false;
    bool listen = false;
    bool gainQEnabled = false;

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

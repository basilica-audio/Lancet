#pragma once

#include "AdaaTanh.h"
#include "Detector.h"
#include "TptSvf.h"

#include <juce_dsp/juce_dsp.h>

#include <atomic>
#include <limits>

// One dynamic-EQ band: a bell/shelf filter (a trapezoidal state-variable
// filter, see TptSvf.h) whose gain is `static gain + dynamic gain`, where
// the dynamic component is derived from a Detector (an envelope of either
// the plugin's pre-chain input or an external sidechain bus, optionally
// bandpass-filtered to this band's own frequency/Q - see Detector.h)
// crossed against Threshold with a soft knee and clamped to +-Range.
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
//==============================================================================
// v0.4.0: the ballistics-true per-sample gain path (SOTA brief F1/F2)
//
// Up to v0.3.0 this band ran an RBJ biquad whose coefficients were rebuilt
// once per 32-sample sub-block from a 50 ms-smoothed gain value. That
// smoother was a hidden second set of ballistics sitting *after* the user's
// Attack/Release knobs, so no dialed Attack faster than ~50 ms could ever be
// heard: the detector reacted in 0.5 ms and the filter then took 50 ms to
// get there anyway. The Attack range shipped since v0.2.0 goes down to
// 0.1 ms, so most of that range did nothing. v0.4.0 removes the smoother and
// evaluates the whole chain - envelope -> dB -> gain computer -> filter gain
// - once per sample, which is what makes the dialed Attack the *only* thing
// shaping how fast the band moves.
//
// Removing a smoother from a coefficient-rebuilding biquad would be a
// zipper-noise generator, so the filter core changed at the same time: the
// band is now a TPT state-variable filter (TptSvf.h), whose integrator state
// keeps its meaning under per-sample coefficient modulation. Frequency
// enters through a per-sub-block `g = tan(pi*f0/fs)`; only the gain-derived
// scalars move per sample. The realised static response is identical to the
// RBJ biquad it replaces (see TptSvf.h's derivation), so existing static EQ
// settings, sessions and presets keep exactly the curve they had.
//
// Consequence, stated plainly: a session that used Range != 0 with an Attack
// faster than ~50 ms *will* sound different - faster - after this change.
// That is the defect being fixed, not a new voicing decision. Sessions with
// Range == 0 on every band (pure static EQ) are unchanged.
//
// What is still smoothed, and why:
//   - the *static* Gain knob, through a 15 ms one-pole, per sample. This
//     exists only so that host automation of Gain does not step; it is not
//     in the dynamics path at all.
//   - Frequency and Q, through a 20 ms one-pole evaluated once per
//     sub-block. The SVF is stable under stepped coefficient updates; this
//     ramp only makes knob moves inaudible.
//   - the Gain/Q coupling's own input, through a further 30 ms one-pole
//     (see the `gainQ` paragraph below).
//   - the dynamic gain itself: NOT smoothed. The detector envelope *is* the
//     smoother, which is the whole point.
//
//==============================================================================
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
//     v0.4.0 (SOTA brief F6) feeds this coupling from a 30 ms-smoothed copy
//     of the dynamic gain rather than the raw instantaneous value, so the
//     realised Q glides instead of taking a full-range step at every
//     sub-block boundary.
//
// v0.3.0 (docs/voicing-notes.md) adds a third opt-in, off-by-default
// per-band boolean: `sat`. When on, a gentle waveshaper is applied to
// this band's own post-filter samples, but ONLY while the band is actively
// boosting - i.e. `on == true` and the combined (static + dynamic) applied
// gain is strictly positive. A cutting or idle band is completely
// unaffected even with `sat` on (see computeSaturationDrive() below) - this
// is deliberately scoped to "boosted bands" only, per the feature's own
// design intent (a touch of analog-style harmonic warmth on a boost, not a
// general-purpose distortion stage). Drive scales with how much the band is
// currently boosting (0 dB -> a near-transparent low drive, +12 dB -> a
// clearly audible but still gentle drive), so small boosts stay nearly
// clean and larger ones read as intentionally warmed. v0.4.0 (SOTA brief
// F5) replaces the memoryless tanh with an antiderivative-antialiased
// (ADAA1) tanh kernel of the same shape - same harmonic character, less
// fold-back, still zero latency and no oversampling stage (AdaaTanh.h).
//
// v0.4.0 also adds two per-band detector-routing controls (SOTA brief
// F3/F4), both defaulting to the pre-v0.4.0 behaviour:
//   - `sidechainExternal`: take the detector's input from the plugin's
//     optional external sidechain bus instead of the pre-chain tap. Falls
//     back to the pre-chain tap whenever the host provides no sidechain, so
//     the band never goes silent or NaN because a bus is missing.
//   - `detectorWide`: bypass the detector's own bandpass so the band follows
//     full-range programme level (Detector::setSplitMode()).
//
// Coefficient updates for the Detector's bandpass and the SVF's frequency
// warp are real-time safe (see RealtimeCoefficients.h / TptSvf.h) and are
// only ever done once per `processSubBlock()` call, and then only when the
// smoothed frequency/Q have actually moved (v0.4.0, SOTA brief F8) - the
// caller (LancetEngine) is responsible for chunking a full block into
// <= 32-sample sub-blocks, per docs/design-brief.md.
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

    // Allocates all DSP state (Detector, filter, saturator, listen buffer).
    // Must be called before the first processSubBlock() call, and again
    // whenever sample rate/block size/channel count change.
    void prepare (const juce::dsp::ProcessSpec& spec);

    // Clears filter/envelope/smoother state without deallocating. Safe to
    // call from the audio thread.
    void reset();

    // Processes one <= 32-sample sub-block. `mainSubBlock` is this band's
    // slice of the serial main signal path (processed in place if the band
    // is on; left untouched otherwise - a true bypass, not just a 0 dB
    // filter, so an off band is bit-identical). `preChainBlock` is the
    // *whole* current plugin block's pre-chain tap (see LancetEngine);
    // `sidechainBlock` is the same block's external sidechain feed, or an
    // empty block when the host provides none. `startSample`/`numSamples`
    // select this call's slice of both, matching `mainSubBlock`'s own
    // extent. Real-time safe: no allocation once prepare() has completed.
    void processSubBlock (juce::dsp::AudioBlock<float> mainSubBlock,
                           const juce::dsp::AudioBlock<const float>& preChainBlock,
                           const juce::dsp::AudioBlock<const float>& sidechainBlock,
                           size_t startSample,
                           size_t numSamples) noexcept;

    // Convenience overload for callers with no external sidechain (and for
    // every pre-v0.4.0 test): equivalent to passing an empty sidechain
    // block, i.e. the detector always reads the pre-chain tap.
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
    // Attack/Release are remembered here as well as pushed into the Detector
    // because the Detector derives its coefficients from the sample rate:
    // prepare() has to re-apply them at the new rate, and it must re-apply
    // the *current* values rather than a hardcoded pair (which is what every
    // build up to v0.3.0 did, silently resetting a band's ballistics to
    // 5 ms/150 ms on every prepare until the next parameter push arrived).
    void setAttackMs (float newAttackMs) noexcept
    {
        attackMs = newAttackMs;
        detector.setAttackMs (newAttackMs);
    }

    void setReleaseMs (float newReleaseMs) noexcept
    {
        releaseMs = newReleaseMs;
        detector.setReleaseMs (newReleaseMs);
    }
    void setListen (bool shouldListen) noexcept { listen = shouldListen; }
    void setAutoRelease (bool shouldAutoRelease) noexcept { detector.setAutoRelease (shouldAutoRelease); }
    void setGainQ (bool shouldCoupleGainToQ) noexcept { gainQEnabled = shouldCoupleGainToQ; }
    void setSaturation (bool shouldSaturateOnBoost) noexcept { saturationEnabled = shouldSaturateOnBoost; }

    // v0.4.0 (SOTA brief F3/F4) - see the class comment.
    void setSidechainExternal (bool shouldUseExternalSidechain) noexcept { sidechainExternal = shouldUseExternalSidechain; }
    void setDetectorWide (bool shouldDetectFullRange) noexcept { detectorWide = shouldDetectFullRange; }

    bool isListening() const noexcept { return listen; }
    bool isOn() const noexcept { return on; }
    bool isSidechainExternal() const noexcept { return sidechainExternal; }
    bool isDetectorWide() const noexcept { return detectorWide; }

    // The band's own detector feed, one full plugin block's worth - used to
    // substitute the final output when this band's Listen is engaged (see
    // LancetEngine::process()). Because Detector writes whatever the
    // envelope follower actually hears, this automatically follows the
    // band's SC Source and SC Mode: band-passed pre-chain audio in Split,
    // full-range in Wide, the sidechain bus when SC Source is External.
    const juce::AudioBuffer<float>& getListenBuffer() const noexcept { return detector.getListenBuffer(); }

    float getLastDetectorLevelDb() const noexcept { return detector.getLastLevelDb(); }

    // v0.4.0 (SOTA brief F7): the dynamic (detector-driven) component of the
    // gain this band applied at the last sample of the most recent
    // sub-block, in dB - negative while cutting, positive while boosting,
    // 0 when the band is idle or Range == 0. Written once per sub-block with
    // a relaxed store and read from the message thread, mirroring
    // getLastDetectorLevelDb()'s role for the future M3 gain-reduction
    // needle.
    float getLastAppliedDynamicGainDb() const noexcept { return lastAppliedDynamicGainDb.load (std::memory_order_relaxed); }

    // The Q actually baked into the filter for the most recent sub-block -
    // i.e. the band's own Q once the Gain/Q coupling (if enabled) has had its
    // say. Same relaxed-atomic contract as the gain telemetry above. Exposed
    // so the coupling's smoothness is measurable rather than merely argued
    // for (tests/GainQCouplingTests.cpp), and because an M3 meter that draws
    // the band's live curve needs exactly this number.
    float getLastAppliedFilterQ() const noexcept { return lastAppliedFilterQ.load (std::memory_order_relaxed); }

private:
    // Standard "flat"/Butterworth shelf slope (Q = 1/sqrt(2)), matching the
    // implicit default juce::dsp::IIR::ArrayCoefficients::makeLowShelf/
    // makeHighShelf use when no explicit Q is given. Q is documented as
    // "ignored in shelf mode" in docs/design-brief.md's parameter table;
    // this applies both to the main filter's shape and to the Detector's
    // matched bandpass (see Detector.h/.cpp usage below).
    static constexpr float fixedShelfQ = lnct::TptSvf::defaultShelfQ;

    // Anti-zipper one-pole on the *static* Gain knob only (v0.4.0) - the
    // dynamic term is deliberately unsmoothed, see the class comment.
    static constexpr double staticGainSmoothingSeconds = 0.015;

    // Frequency/Q ramp, applied once per sub-block (v0.4.0). The SVF is
    // stable under stepped coefficient updates, so this exists purely to
    // keep knob moves from being audible as steps.
    static constexpr double coefficientRampSeconds = 0.020;

    // Extra smoothing on the Gain/Q coupling's input (v0.4.0, SOTA brief
    // F6), so the realised Q glides rather than stepping.
    static constexpr double gainQCouplingSeconds = 0.030;

    // Below this, the smoothed static gain snaps to its target exactly.
    // Without it a one-pole never *reaches* 0 dB in finite time, and the
    // exact-0 dB true-bypass path (see processSubBlock) would stop being
    // reachable after any automation move.
    static constexpr float smoothedGainSnapDb = 1.0e-7f;

    // Recompute thresholds for the dirty-flagged coefficient updates
    // (v0.4.0, SOTA brief F8).
    static constexpr float frequencyDirtyEpsilonHz = 1.0e-4f;
    static constexpr float qDirtyEpsilon = 1.0e-6f;

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

    // Saturation drive bounds (v0.3.0, docs/voicing-notes.md): at 0 dB of
    // applied boost, drive sits at saturationDriveFloor - low enough that
    // the waveshaper is very close to identity for ordinary programme
    // levels (a deliberately gentle "just barely there" floor, not a hard
    // on/off switch at 0 dB). At saturationGainReferenceDb (+12 dB, this
    // plugin's own Gain/Range ceiling) drive reaches saturationDriveCeiling,
    // a clearly audible but still soft-knee-shaped (not hard-clipped)
    // saturation. Both numbers are this feature's own engineering judgment,
    // not sourced from a reference plugin - see docs/voicing-notes.md's
    // honesty section.
    static constexpr float saturationDriveFloor = 0.3f;
    static constexpr float saturationDriveCeiling = 2.5f;
    static constexpr float saturationGainReferenceDb = 12.0f;

    bool isEffectivelyShelf() const noexcept { return shelfDirection != ShelfDirection::none && shelfSelected; }
    float effectiveQ() const noexcept { return isEffectivelyShelf() ? fixedShelfQ : q; }

    lnct::TptSvf::Type currentSvfType() const noexcept;

    // The Q actually baked into the main filter's coefficients: the smoothed
    // effectiveQ(), optionally narrowed toward gainQMinMultiplier as
    // `dynamicGainDbAbs` approaches |Range| - see class comment's "gainQ"
    // paragraph for why this is scoped away from the Detector's own
    // bandpass Q.
    float computeMainFilterQ (float dynamicGainDbAbs) const noexcept;

    // Knee width in dB for the current Range setting - see class comment
    // and kneeWidthFloorDb/kneeWidthCeilingDb/kneeWidthRangeSlope above.
    float computeKneeWidthDb() const noexcept;

    // Soft-knee gain-computer overshoot (see class comment), always >= 0.
    static float softKneeOvershoot (float overshootDb, float kneeWidthDb) noexcept;

    // Saturation drive (0..saturationDriveCeiling) for a given positive
    // applied-gain value in dB - see class comment and the bounds above.
    // `positiveGainDb` must already be > 0 (callers only invoke this while
    // boosting).
    float computeSaturationDrive (float positiveGainDb) const noexcept;

    // One-pole coefficient (per step) for a given time constant, at the
    // given step rate. Returns a value in (0, 1]; the smoother is then
    // `value += (target - value) * coefficient`.
    static float onePoleCoefficient (double timeConstantSeconds, double stepsPerSecond) noexcept;

    ShelfDirection shelfDirection;

    lnct::TptSvf svf;
    lnct::AdaaTanh saturator;

    Detector detector;

    double sampleRate = 44100.0;

    bool on = false;
    bool shelfSelected = false;
    bool listen = false;
    bool gainQEnabled = false;
    bool saturationEnabled = false;
    bool sidechainExternal = false;
    bool detectorWide = false;

    float frequencyHz = 1000.0f;
    float q = 1.0f;
    float staticGainDb = 0.0f;
    float rangeDb = 0.0f;
    float thresholdDb = -30.0f;

    // Mirrors of the two Detector ballistics settings, kept so prepare() can
    // re-derive their sample-rate-dependent coefficients - see setAttackMs().
    // The initial values are the historical prepare()-time defaults, so a
    // band that is never told otherwise behaves exactly as before.
    float attackMs = 5.0f;
    float releaseMs = 150.0f;

    // Smoothed control values (see the class comment for each one's role).
    float staticGainDbSmoothed = 0.0f;
    float frequencyHzSmoothed = 1000.0f;
    float qSmoothed = 1.0f;
    float dynamicGainDbAbsSmoothed = 0.0f;

    float staticGainCoefficient = 1.0f;
    float subBlockRampCoefficient = 1.0f;
    float gainQCouplingCoefficient = 1.0f;

    // Frequency warp for the SVF, recomputed only when the smoothed
    // frequency (or the sample rate) actually moves - SOTA brief F8.
    double gBase = 0.0;
    float gBaseFrequencyHz = -1.0f;
    float detectorCoefficientFrequencyHz = -1.0f;
    float detectorCoefficientQ = -1.0f;

    // Per-sample coefficient cache: the SVF scalars only depend on
    // (type, gBase, Q, total gain), and the first three are constant within
    // a sub-block, so a band sitting at a settled gain recomputes nothing at
    // all. `cachedTotalGainDb` starts at NaN so the first comparison always
    // misses.
    lnct::TptSvf::Coefficients cachedCoefficients {};
    float cachedTotalGainDb = std::numeric_limits<float>::quiet_NaN();
    float cachedQ = std::numeric_limits<float>::quiet_NaN();
    double cachedGBase = -1.0;
    lnct::TptSvf::Type cachedType = lnct::TptSvf::Type::bell;
    bool coefficientCacheValid = false;

    std::atomic<float> lastAppliedDynamicGainDb { 0.0f };
    std::atomic<float> lastAppliedFilterQ { 1.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DynamicBand)
};

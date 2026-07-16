#pragma once

#include <juce_dsp/juce_dsp.h>

#include <vector>

// One band's sidechain detector: a bandpass filter matched to the band's
// frequency/Q, tapped from the plugin's pre-chain input (not the band's own
// serially-processed audio - see LancetEngine), feeding a peak envelope
// follower with independent attack/release ballistics.
//
// The bandpass stage is *two* cascaded juce::dsp::IIR::Filter<float>
// biquads sharing one juce::dsp::IIR::ArrayCoefficients<float>::makeBandPass
// coefficient set (4th-order effective response), not a single biquad.
// A single RBJ bandpass biquad only reaches ~-12 dB attenuation two octaves
// from center at Q=1 (6 dB/octave/side asymptotic slope), which does not
// meet the design brief's detector-isolation guarantee ("out-of-band tone
// must not trigger the band, >20 dB/oct selectivity at 2 octaves for Q=1");
// cascading the same bandpass twice roughly doubles the dB attenuation
// (measured ~-24 dB at 2 octaves for Q=1, see tests/DetectorTests.cpp),
// clearing that bar with margin.
//
// Stereo (or wider) input is *linked*: the two cascade stages run
// independently per channel (their own filter state each), but the
// envelope follower is a single band-wide value fed by the loudest
// (max-abs) sample across channels at each instant - this avoids the
// stereo image shifting that independent per-channel gain reduction would
// otherwise introduce, matching how a stereo-linked dynamic EQ (e.g. the
// Waves F6 in its default linked mode) behaves.
//
// Filter coefficients are updated in place via lnct::applyBiquadCoefficients
// (RealtimeCoefficients.h) using juce::dsp::IIR::ArrayCoefficients - no heap
// allocation once prepare() has run, so setFrequency/setQ are safe to call
// every block from the audio thread.
class Detector
{
public:
    Detector() = default;

    // Allocates per-channel filter state and the listen/bandpass output
    // buffer (sized to the maximum block declared in `spec`). Must be
    // called before the first processSubBlock() call, and again whenever
    // sample rate/block size/channel count change.
    void prepare (const juce::dsp::ProcessSpec& spec);

    // Clears filter and envelope state without deallocating. Safe to call
    // from the audio thread.
    void reset();

    // Real-time safe: recomputes a stack-only coefficient set and copies it
    // in place into already-allocated storage (see RealtimeCoefficients.h).
    // `q` should already be the *effective* Q (i.e. the fixed 0.707 shelf Q
    // when the owning band is in Shelf mode - see DynamicBand.cpp).
    void setFrequencyAndQ (float frequencyHz, float q) noexcept;

    void setAttackMs (float attackMs) noexcept;
    void setReleaseMs (float releaseMs) noexcept;

    // Program-dependent auto-release (v0.2.0, docs/design-brief.md §2/§3).
    // Off by default - when off, processSubBlock() below runs the exact
    // same floating-point operations on `envelopeLinear`/`lastLevelDb` as
    // v0.1.0 (the auto-release-only branches are simply skipped), which is
    // what guarantee #1's tolerant-import/inertness null test relies on.
    //
    // Mechanism (this class's own concrete implementation of the brief's
    // "effective release = min(user Release-ms, a value derived from the
    // detector envelope's own recent fall rate)" - inspired by, not a
    // reproduction of, Waves F6's proprietary ARC, per the design brief's
    // honesty section): a second, always-on internal envelope
    // (`fastReferenceEnvelopeLinear`) tracks the same rectified signal with
    // the *same* Attack coefficient as the main envelope but a *fixed*,
    // Release-setting-independent fast release (tied to this plugin's own
    // Release floor - see Detector.cpp) - a dedicated "as fast as this
    // design ever gets" tracker of what the signal is actually doing right
    // now. Deriving the "recent fall rate" measurement from *that* fast
    // envelope, rather than from the main (possibly very slow, e.g. a
    // musical 500 ms Release) envelope itself, is the key design choice: a
    // slow envelope is, by construction, a low-pass-filtered view of the
    // input that is itself rate-limited to roughly its own release time
    // constant, so measuring "how fast is the slow envelope falling" mostly
    // just measures the slow envelope's own coefficient back at itself,
    // regardless of how fast the true underlying signal is actually moving
    // - an early implementation of this class made exactly that mistake.
    // Once per processSubBlock() call, the fast envelope's own recent dB
    // fall rate is converted to an implied exponential time constant (the
    // tau a pure one-pole decay at that rate would have), clamped to [this
    // plugin's own Release floor, the user's own Release-ms setting], and
    // used as a second, auto-derived release coefficient for a separate
    // output envelope (`outputEnvelopeLinear`) that is what actually feeds
    // the gain computer when this flag is on. A signal whose own level is
    // still genuinely falling when it crosses back under Threshold (e.g. a
    // naturally-decaying transient) keeps measuring a real fall rate for as
    // long as that natural decay continues, keeping the sped-up coefficient
    // engaged; a signal that drops to a new, flat (even if much lower)
    // level and stays there only shows a fall rate for the brief instant
    // the fast envelope itself is catching up to that new level, after
    // which the measured rate collapses back toward zero and this falls
    // back to the plain user coefficient - i.e. "always <= the Release
    // setting," matching F6 ARC's documented promise, never *slower*. See
    // tests/AutoReleaseTests.cpp (design-brief guarantee #3).
    void setAutoRelease (bool shouldAutoRelease) noexcept { autoReleaseEnabled = shouldAutoRelease; }

    // Processes `numSamples` samples (<= the sub-block granularity the
    // caller is using, see DynamicBand::processSubBlock) starting at
    // `startSample` within both `preChainBlock` (read-only input, tapped
    // pre-chain by the engine) and this detector's own listen/bandpass
    // output buffer (see getListenBuffer()). Runs the cascaded bandpass
    // filter and the linked peak envelope follower sample-by-sample (the
    // envelope needs per-sample ballistics for correct attack/release
    // timing; only filter *coefficient* updates are throttled to the
    // caller's sub-block granularity - see setFrequencyAndQ). Returns the
    // envelope level in dBFS after processing this range (-100 dBFS floor).
    //
    // Real-time safe: no allocation once prepare() has completed. A
    // zero-sample range is a safe no-op that just returns the last level.
    float processSubBlock (const juce::dsp::AudioBlock<const float>& preChainBlock,
                            size_t startSample,
                            size_t numSamples) noexcept;

    float getLastLevelDb() const noexcept { return lastLevelDb; }

    // The cascaded bandpass filter's own output, one full plugin block's
    // worth (written progressively by processSubBlock calls across a
    // block). Used for the "Listen" (exclusive sidechain solo) feature -
    // see LancetEngine::process().
    const juce::AudioBuffer<float>& getListenBuffer() const noexcept { return listenBuffer; }

private:
    static constexpr float minusInfinityDb = -100.0f;

    double sampleRate = 44100.0;

    // Both cascade stages share one Coefficients object (constructed once
    // via the 6-argument form, updated in place thereafter) - a bandpass
    // filter applied twice at the same frequency/Q, so both stages are
    // identical and can share the same coefficient storage; only their
    // internal per-channel state differs.
    juce::dsp::IIR::Coefficients<float>::Ptr bandpassCoefficients {
        new juce::dsp::IIR::Coefficients<float> (1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f)
    };

    std::vector<juce::dsp::IIR::Filter<float>> stage1;
    std::vector<juce::dsp::IIR::Filter<float>> stage2;

    juce::AudioBuffer<float> listenBuffer;

    // Linked (single, not per-channel) envelope state - see class comment.
    float envelopeLinear = 0.0f;
    float lastLevelDb = minusInfinityDb;

    float attackCoefficient = 0.0f;
    float releaseCoefficient = 0.0f;
    float userReleaseMs = 150.0f;

    // Program-dependent auto-release state (see setAutoRelease()'s docs).
    // The absolute floor of the auto-derived effective release, tied to
    // this plugin's own Release parameter floor (ParameterLayout.cpp) so
    // it's a self-consistent reference rather than an arbitrary extra
    // magic number.
    static constexpr float autoReleaseFloorMs = 5.0f;

    bool autoReleaseEnabled = false;
    float outputEnvelopeLinear = 0.0f;
    float autoReleaseCoefficient = 0.0f;
    float previousReferenceLevelDbForAutoRelease = minusInfinityDb;
    size_t previousSubBlockNumSamplesForAutoRelease = 0;
    bool haveAutoReleaseReference = false;

    // Fast reference envelope (see setAutoRelease()'s docs) - same Attack
    // coefficient as the user's own, but a fixed, Release-setting-
    // independent fast release, so it tracks the true signal's own recent
    // behaviour closely regardless of how slow the user's Release is.
    // fastReleaseCoefficient is derived once (prepare()/whenever sampleRate
    // changes) from autoReleaseFloorMs, independent of setReleaseMs().
    float fastEnvelopeLinear = 0.0f;
    float fastReleaseCoefficient = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Detector)
};

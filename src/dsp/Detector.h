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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Detector)
};

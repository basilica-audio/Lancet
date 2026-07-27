#include "dsp/Detector.h"
#include "dsp/LancetEngine.h"
#include "TestHelpers.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <vector>

// v0.4.0 (SOTA brief F1/T1): "the Attack knob is actually true".
//
// Up to v0.3.0 a band's realised gain came from a 50 ms juce::SmoothedValue
// sitting *after* the gain computer, so every Attack setting faster than
// ~50 ms produced the same audible result - most of the shipped 0.1-500 ms
// range did nothing. v0.4.0 removes that smoother and evaluates the whole
// chain (envelope -> dB -> gain computer -> filter gain) per sample. This
// file is the measurement that says so, at two levels:
//
//   (a) Detector unit level - the envelope follower's own time constant,
//       measured directly on the linear-domain envelope, which is where
//       "tau" is defined. Nothing between the knob and the envelope may
//       stretch it.
//   (b) Engine level - the band's realised gain-reduction trajectory must
//       arrive on the schedule the detector's tau implies, i.e. no
//       *additional* smoothing has been reintroduced between envelope and
//       filter. This is the assertion that would have failed loudly against
//       v0.3.0.
//
//==============================================================================
// Stimulus choice, and why it is not the sine the brief nominated
//
// The brief specifies a 1 kHz tone stepping between two levels. Measuring an
// absolute t63 against a *sine* does not work, and the reason is a property
// of peak detection rather than of this implementation:
//
// The detector is a peak follower, so its attack branch only runs on the
// samples where the rectified input currently exceeds the envelope. Writing
// u = env/peak, the mean per-sample drive over one cycle of a sine is
//
//     E(u) = (2/pi) * ( sqrt(1-u^2) - u*acos(u) )
//
// which is 0.64 at u = 0 and collapses towards 0 as u -> 1 (at u = 0.95 it is
// already 0.0067, and at u = 0.99 it is 0.00057). The envelope therefore
// approaches the tone's peak asymptotically-slowly no matter how fast the
// dialed Attack is: integrating du/E(u) puts t63 at ~2.16*tau and t95 at
// ~16*tau. Both numbers are stimulus properties, identical for every Attack
// setting - which is exactly why an absolute "t63 within +-30% of tau" or
// "95% within 5*tau" assertion against a sine fails a *correct*
// implementation. (Measured on this build before the stimulus was changed:
// t63/tau came out at 2.16 for a 5 ms Attack, matching the integral above to
// three digits, and the engine-level 95% point sat at ~46 ms for both a
// 0.5 ms and a 5 ms Attack - i.e. dominated entirely by the sine's own
// asymptote, measuring nothing about the plugin.)
//
// The fix is to remove the stimulus from the measurement: every absolute
// timing assertion below uses a *constant-magnitude* stimulus - a 1 kHz
// square wave, whose rectified value is the same on every sample - fed
// through the detector in Wide mode (v0.4.0's own full-range detection, so
// the bandpass does not reshape it). Against a constant magnitude the peak
// follower is a textbook one-pole: t63 == tau and t37 == tau exactly, and the
// numbers below mean what they say. The band-passed sine case is still
// covered, by the ordering assertions, where the stimulus bias cancels.
//
// This is the same class of correction the brief's own revision note 1 made
// for the original T1, applied one level deeper; it is called out in the PR
// description as a deviation.
namespace
{
    constexpr double testSampleRate = 48000.0;
    constexpr double toneHz = 1000.0;

    juce::dsp::ProcessSpec makeSpec (int maxSamples, int numChannels = 1)
    {
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = testSampleRate;
        spec.maximumBlockSize = static_cast<juce::uint32> (maxSamples);
        spec.numChannels = static_cast<juce::uint32> (numChannels);
        return spec;
    }

    float dbToLinear (float db) { return std::pow (10.0f, db / 20.0f); }

    // A square wave: |x[n]| == amplitude on every single sample, so a peak
    // follower sees a perfectly flat target. See the header comment.
    void fillWithSquare (juce::AudioBuffer<float>& buffer,
                          double frequencyHz,
                          float amplitude,
                          juce::int64 startSampleIndex = 0)
    {
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            auto* data = buffer.getWritePointer (channel);

            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                const auto phase = frequencyHz * static_cast<double> (startSampleIndex + i) / testSampleRate;
                const auto fractional = phase - std::floor (phase);
                data[i] = fractional < 0.5 ? amplitude : -amplitude;
            }
        }
    }

    // Runs one sample at a time so the envelope can be sampled at full rate,
    // and returns the linear-domain envelope trajectory.
    std::vector<float> measureEnvelopeTrajectory (Detector& detector, const juce::AudioBuffer<float>& stimulus)
    {
        const juce::dsp::AudioBlock<const float> block (stimulus);

        std::vector<float> envelope;
        envelope.reserve (static_cast<size_t> (stimulus.getNumSamples()));

        detector.beginSubBlock();

        for (int i = 0; i < stimulus.getNumSamples(); ++i)
            envelope.push_back (dbToLinear (detector.processSample (block, static_cast<size_t> (i))));

        detector.endSubBlock (static_cast<size_t> (stimulus.getNumSamples()));

        return envelope;
    }

    int firstCrossingRising (const std::vector<float>& trajectory, float target)
    {
        for (size_t i = 0; i < trajectory.size(); ++i)
            if (trajectory[i] >= target)
                return static_cast<int> (i);

        return -1;
    }

    int firstCrossingFalling (const std::vector<float>& trajectory, float target)
    {
        for (size_t i = 0; i < trajectory.size(); ++i)
            if (trajectory[i] <= target)
                return static_cast<int> (i);

        return -1;
    }

    // t63 of the detector's linear envelope for a -40 dBFS -> 0 dBFS step of
    // a constant-magnitude stimulus, in milliseconds. Against a flat target
    // this is the textbook one-pole time constant.
    double measureAttackT63Ms (float attackMs)
    {
        const auto settleSamples = static_cast<int> (testSampleRate);                          // 1 s
        const auto stepSamples = juce::jmax (4800, static_cast<int> (0.02 * attackMs * testSampleRate)); // >= 20x tau

        Detector detector;
        detector.prepare (makeSpec (juce::jmax (settleSamples, stepSamples)));
        detector.setSplitMode (false); // Wide: no bandpass shaping of the stimulus
        detector.setAttackMs (attackMs);
        detector.setReleaseMs (2000.0f); // irrelevant to the attack phase, deliberately slow

        constexpr float quietAmplitude = 0.01f; // -40 dBFS
        constexpr float loudAmplitude = 1.0f;   // 0 dBFS

        juce::AudioBuffer<float> quiet (1, settleSamples);
        fillWithSquare (quiet, toneHz, quietAmplitude);
        const juce::dsp::AudioBlock<const float> quietBlock (quiet);
        detector.processSubBlock (quietBlock, 0, static_cast<size_t> (settleSamples));

        const auto startEnvelope = dbToLinear (detector.getLastLevelDb());

        juce::AudioBuffer<float> loud (1, stepSamples);
        fillWithSquare (loud, toneHz, loudAmplitude, settleSamples);

        const auto trajectory = measureEnvelopeTrajectory (detector, loud);
        REQUIRE_FALSE (trajectory.empty());

        const auto target = startEnvelope + 0.63212f * (loudAmplitude - startEnvelope);
        const auto crossing = firstCrossingRising (trajectory, target);
        REQUIRE (crossing >= 0);

        return 1000.0 * static_cast<double> (crossing) / testSampleRate;
    }

    // t37 of the detector's linear envelope for a 0 dBFS -> silence step.
    double measureReleaseT37Ms (float releaseMs)
    {
        const auto settleSamples = static_cast<int> (testSampleRate);
        const auto decaySamples = juce::jmax (4800, static_cast<int> (0.02 * releaseMs * testSampleRate));

        Detector detector;
        detector.prepare (makeSpec (juce::jmax (settleSamples, decaySamples)));
        detector.setSplitMode (false);
        detector.setAttackMs (0.1f); // irrelevant to the release phase, deliberately fast
        detector.setReleaseMs (releaseMs);

        juce::AudioBuffer<float> loud (1, settleSamples);
        fillWithSquare (loud, toneHz, 1.0f);
        const juce::dsp::AudioBlock<const float> loudBlock (loud);
        detector.processSubBlock (loudBlock, 0, static_cast<size_t> (settleSamples));

        const auto startEnvelope = dbToLinear (detector.getLastLevelDb());
        REQUIRE (startEnvelope > 0.9f);

        juce::AudioBuffer<float> silence (1, decaySamples);
        silence.clear();

        const auto trajectory = measureEnvelopeTrajectory (detector, silence);

        const auto crossing = firstCrossingFalling (trajectory, startEnvelope * 0.36788f);
        REQUIRE (crossing >= 0);

        return 1000.0 * static_cast<double> (crossing) / testSampleRate;
    }

    // Same measurement, but through the shipped default path: Split mode, so
    // the band-matched cascaded bandpass is in circuit, driven by a sine at
    // the band centre. Absolute values here carry the stimulus bias described
    // in the header comment, so only ordering is asserted against them.
    double measureSplitModeToneT63Ms (float attackMs)
    {
        const auto settleSamples = static_cast<int> (testSampleRate);
        const auto stepSamples = juce::jmax (24000, static_cast<int> (0.05 * attackMs * testSampleRate));

        Detector detector;
        detector.prepare (makeSpec (juce::jmax (settleSamples, stepSamples)));
        detector.setFrequencyAndQ (static_cast<float> (toneHz), 1.0f);
        detector.setAttackMs (attackMs);
        detector.setReleaseMs (2000.0f);

        juce::AudioBuffer<float> quiet (1, settleSamples);
        TestHelpers::fillWithSine (quiet, testSampleRate, toneHz, 0.01f);
        const juce::dsp::AudioBlock<const float> quietBlock (quiet);
        detector.processSubBlock (quietBlock, 0, static_cast<size_t> (settleSamples));

        const auto startEnvelope = dbToLinear (detector.getLastLevelDb());

        juce::AudioBuffer<float> loud (1, stepSamples);
        TestHelpers::fillWithSine (loud, testSampleRate, toneHz, 1.0f, settleSamples);

        const auto trajectory = measureEnvelopeTrajectory (detector, loud);
        const auto target = startEnvelope + 0.63212f * (1.0f - startEnvelope);
        const auto crossing = firstCrossingRising (trajectory, target);
        REQUIRE (crossing >= 0);

        return 1000.0 * static_cast<double> (crossing) / testSampleRate;
    }

    //==========================================================================
    // (b) Engine-level gain-reduction trajectory, read from the per-band
    // applied-dynamic-gain telemetry (SOTA brief F7) every 8 samples.

    struct GrTrajectory
    {
        std::vector<float> grDb; // magnitude of the applied dynamic gain, dB
        double stepMs = 0.0;
    };

    GrTrajectory measureEngineGrTrajectory (float attackMs, float rangeDb, float overshootDb)
    {
        constexpr int stepSamples = 8; // 0.167 ms of trajectory resolution
        constexpr float amplitude = 0.3f;

        const auto quietBlocks = 6000;
        const auto loudBlocks = juce::jmax (1200, static_cast<int> (0.03 * attackMs * testSampleRate / stepSamples));

        const auto levelDbfs = juce::Decibels::gainToDecibels (amplitude);

        LancetEngine engine;
        engine.setBandOn (2, true);
        engine.setBandFrequencyHz (2, static_cast<float> (toneHz));
        engine.setBandQ (2, 1.0f);
        engine.setBandGainDb (2, 0.0f);
        engine.setBandRangeDb (2, rangeDb);
        engine.setBandThresholdDb (2, levelDbfs - overshootDb);
        engine.setBandAttackMs (2, attackMs);
        engine.setBandReleaseMs (2, 500.0f);
        // Wide detection, for the constant-magnitude stimulus reasoning in
        // this file's header comment.
        engine.setBandDetectorWide (2, true);
        engine.setInputTrimDb (0.0f);
        engine.setOutputTrimDb (0.0f);
        engine.setMixPercent (100.0f);
        engine.prepare (makeSpec (stepSamples));

        juce::AudioBuffer<float> buffer (1, stepSamples);

        // Settle well below threshold so the dynamic term starts at 0.
        for (int block = 0; block < quietBlocks; ++block)
        {
            fillWithSquare (buffer, toneHz, amplitude * dbToLinear (-40.0f),
                             static_cast<juce::int64> (block) * stepSamples);
            juce::dsp::AudioBlock<float> audioBlock (buffer);
            engine.process (audioBlock);
        }

        GrTrajectory result;
        result.stepMs = 1000.0 * stepSamples / testSampleRate;
        result.grDb.reserve (static_cast<size_t> (loudBlocks));

        for (int block = 0; block < loudBlocks; ++block)
        {
            fillWithSquare (buffer, toneHz, amplitude,
                             static_cast<juce::int64> (quietBlocks + block) * stepSamples);
            juce::dsp::AudioBlock<float> audioBlock (buffer);
            engine.process (audioBlock);

            result.grDb.push_back (std::abs (engine.getLastAppliedDynamicGainDb (2)));
        }

        return result;
    }

    double timeToFractionMs (const GrTrajectory& trajectory, double fraction)
    {
        REQUIRE_FALSE (trajectory.grDb.empty());

        const auto settled = static_cast<double> (trajectory.grDb.back());
        REQUIRE (settled > 0.0);

        const auto target = static_cast<float> (settled * fraction);

        for (size_t i = 0; i < trajectory.grDb.size(); ++i)
            if (trajectory.grDb[i] >= target)
                return static_cast<double> (i) * trajectory.stepMs;

        return std::numeric_limits<double>::infinity();
    }
}

//==============================================================================
// (a) Detector-unit tau fidelity.

TEST_CASE ("Attack fidelity: the detector envelope's t63 matches the dialed Attack across the whole range",
           "[dsp][attack-fidelity]")
{
    // +-30% against a constant-magnitude stimulus, where t63 == tau exactly
    // in theory. A fixed 50 ms bottleneck anywhere in this path would put
    // every setting below 50 ms wildly outside the window.
    for (const auto attackMs : { 0.1f, 0.5f, 5.0f, 50.0f, 200.0f })
    {
        const auto measuredMs = measureAttackT63Ms (attackMs);
        const auto ratio = measuredMs / static_cast<double> (attackMs);

        INFO ("Attack = " << attackMs << " ms, measured t63 = " << measuredMs << " ms, ratio = " << ratio);
        CHECK (ratio > 0.7);
        CHECK (ratio < 1.3);
    }
}

TEST_CASE ("Attack fidelity: at the 0.1 ms floor the detector envelope reaches t63 within 1 ms",
           "[dsp][attack-fidelity]")
{
    // The headline v0.4.0 claim as an absolute number: the fastest Attack the
    // plugin offers really is sub-millisecond at the source. Under v0.3.0's
    // 50 ms gain smoother the realised gain could not have got there in under
    // ~50 ms no matter what the detector did.
    const auto measuredMs = measureAttackT63Ms (0.1f);

    INFO ("Attack = 0.1 ms, measured t63 = " << measuredMs << " ms");
    CHECK (measuredMs <= 1.0);
}

TEST_CASE ("Attack fidelity: the detector envelope's t37 matches the dialed Release", "[dsp][attack-fidelity]")
{
    for (const auto releaseMs : { 5.0f, 100.0f, 1000.0f })
    {
        const auto measuredMs = measureReleaseT37Ms (releaseMs);
        const auto ratio = measuredMs / static_cast<double> (releaseMs);

        INFO ("Release = " << releaseMs << " ms, measured t37 = " << measuredMs << " ms, ratio = " << ratio);
        CHECK (ratio > 0.7);
        CHECK (ratio < 1.3);
    }
}

TEST_CASE ("Attack fidelity: measured t63 is strictly monotone across the Attack range, in the shipped "
           "Split/band-passed path too",
           "[dsp][attack-fidelity]")
{
    // Absolute values here carry the sine/peak-follower bias described in the
    // header comment, but the bias is the same for every setting, so strict
    // ordering still proves the knob is doing the work - through the default
    // detector path, cascaded bandpass and all.
    double previousMs = 0.0;

    for (const auto attackMs : { 0.1f, 0.5f, 5.0f, 50.0f, 200.0f, 500.0f })
    {
        const auto measuredMs = measureSplitModeToneT63Ms (attackMs);
        INFO ("Attack = " << attackMs << " ms, measured Split-mode t63 = " << measuredMs << " ms");
        CHECK (measuredMs > previousMs);
        previousMs = measuredMs;
    }
}

//==============================================================================
// (b) Engine level: no smoothing has been reintroduced behind the detector.

TEST_CASE ("Attack fidelity: engine gain reduction reaches 95% of steady state within 5x the dialed Attack",
           "[dsp][attack-fidelity]")
{
    // 6 dB of overshoot against a -12 dB Range keeps the gain computer
    // strictly in its linear region, so the gain-reduction trajectory is the
    // envelope's own trajectory read through a monotone dB map. A bare
    // one-pole crosses 95% at ~3 tau; 5 tau leaves margin for the knee.
    //
    // This is the assertion v0.3.0 could not have passed: with a 50 ms
    // smoother behind the gain computer, the 0.5 ms case would have needed
    // ~150 ms against a 2.5 ms budget.
    for (const auto attackMs : { 0.5f, 5.0f, 50.0f })
    {
        const auto trajectory = measureEngineGrTrajectory (attackMs, -12.0f, 6.0f);
        const auto reachedMs = timeToFractionMs (trajectory, 0.95);

        INFO ("Attack = " << attackMs << " ms, 95% of steady-state GR reached at " << reachedMs << " ms");
        CHECK (reachedMs <= 5.0 * static_cast<double> (attackMs));
    }
}

TEST_CASE ("Attack fidelity: engine gain-reduction rise time is strictly ordered by the dialed Attack",
           "[dsp][attack-fidelity]")
{
    double previousMs = -1.0;

    for (const auto attackMs : { 0.5f, 5.0f, 50.0f })
    {
        const auto trajectory = measureEngineGrTrajectory (attackMs, -12.0f, 6.0f);
        const auto reachedMs = timeToFractionMs (trajectory, 0.63);

        INFO ("Attack = " << attackMs << " ms, 63% of steady-state GR reached at " << reachedMs << " ms");
        CHECK (reachedMs > previousMs);
        previousMs = reachedMs;
    }
}

TEST_CASE ("Attack fidelity: a 0.1 ms Attack reaches most of its gain reduction inside a millisecond",
           "[dsp][attack-fidelity]")
{
    const auto trajectory = measureEngineGrTrajectory (0.1f, -12.0f, 6.0f);
    const auto reachedMs = timeToFractionMs (trajectory, 0.9);

    INFO ("Attack = 0.1 ms, 90% of steady-state GR reached at " << reachedMs << " ms");
    CHECK (reachedMs <= 1.0);
}

TEST_CASE ("Attack fidelity: auto-release still never runs slower than the manual Release setting",
           "[dsp][attack-fidelity]")
{
    // Guard for the F1 rework against the v0.2.0 auto-release contract: the
    // per-sample envelope path must not have changed the promise that the
    // derived release is always <= the dialed one.
    constexpr float manualReleaseMs = 400.0f;

    const auto measureDecayMs = [] (bool autoRelease)
    {
        const auto settleSamples = static_cast<int> (testSampleRate);
        const auto decaySamples = static_cast<int> (testSampleRate);

        Detector detector;
        detector.prepare (makeSpec (juce::jmax (settleSamples, decaySamples)));
        detector.setSplitMode (false);
        detector.setAttackMs (1.0f);
        detector.setReleaseMs (manualReleaseMs);
        detector.setAutoRelease (autoRelease);

        juce::AudioBuffer<float> loud (1, settleSamples);
        fillWithSquare (loud, toneHz, 1.0f);
        const juce::dsp::AudioBlock<const float> loudBlock (loud);

        // Sub-block sized calls, so auto-release gets its per-sub-block
        // fall-rate measurements the way the engine feeds it.
        for (int position = 0; position < settleSamples; position += 32)
            detector.processSubBlock (loudBlock, static_cast<size_t> (position), 32);

        const auto startEnvelope = dbToLinear (detector.getLastLevelDb());

        juce::AudioBuffer<float> silence (1, decaySamples);
        silence.clear();
        const juce::dsp::AudioBlock<const float> silentBlock (silence);

        for (int position = 0; position < decaySamples; position += 32)
        {
            detector.processSubBlock (silentBlock, static_cast<size_t> (position), 32);

            if (dbToLinear (detector.getLastLevelDb()) <= startEnvelope * 0.36788f)
                return 1000.0 * static_cast<double> (position) / testSampleRate;
        }

        return 1000.0 * static_cast<double> (decaySamples) / testSampleRate;
    };

    const auto manualMs = measureDecayMs (false);
    const auto autoMs = measureDecayMs (true);

    INFO ("manual t37 = " << manualMs << " ms, auto t37 = " << autoMs << " ms");
    CHECK (autoMs <= manualMs * 1.05);
}

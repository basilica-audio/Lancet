#include "dsp/DynamicBand.h"
#include "TestHelpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

// Guarantee #4 (docs/design-brief.md §4): "Gain/Q coupling proof" - with
// bN_gainQ on and Range nonzero, drive the band to (a) near-zero dynamic
// gain and (b) near-full dynamic gain, measure the band's actual -3 dB
// bandwidth in each state, and assert bandwidth (b) is wider than
// bandwidth (a) by a measurable, non-trivial margin - proves Q is actually
// being modulated by dynamic gain, not just that the parameter is wired but
// numerically inert.
//
// Measurement technique: exercises DynamicBand directly (not through
// LancetEngine) to decouple the *trigger* signal (fed only via
// processSubBlock()'s separate `preChainBlock` argument, locking the
// detector's measured level - and therefore the dynamic gain/Q - at a
// steady-state value) from the *probe* signal being swept to measure the
// actual filter response (fed via `mainSubBlock`). Routing both through the
// full engine instead would confound the two, since the detector would
// react to whatever frequency the probe itself happened to be at.
namespace
{
    constexpr double testSampleRate = 48000.0;
    constexpr float centreFrequencyHz = 1000.0f;
    constexpr float baseQ = 2.0f;
    constexpr float staticGainDb = -12.0f; // a baseline cut always present, so there's a real dip to measure in BOTH states
    constexpr float rangeDb = -12.0f;
    constexpr float thresholdDb = -20.0f;

    constexpr size_t subBlockSamples = 32; // matches LancetEngine's own sub-block granularity
    constexpr int settleSubBlocks = 4000;  // several detector/gain-smoothing time constants
    constexpr int probeSamples = 8192;

    juce::dsp::ProcessSpec makeSpec()
    {
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = testSampleRate;
        spec.maximumBlockSize = static_cast<juce::uint32> (juce::jmax (static_cast<int> (subBlockSamples), probeSamples));
        spec.numChannels = 1;
        return spec;
    }

    void configureBand (DynamicBand& band)
    {
        band.setOn (true);
        band.setFrequencyHz (centreFrequencyHz);
        band.setQ (baseQ);
        band.setStaticGainDb (staticGainDb);
        band.setRangeDb (rangeDb);
        band.setThresholdDb (thresholdDb);
        band.setAttackMs (2.0f);
        band.setReleaseMs (50.0f);
        band.setGainQ (true);
    }

    juce::AudioBuffer<float> makeTriggerBuffer (float triggerDbfs)
    {
        const auto amplitude = 0.5f * juce::Decibels::decibelsToGain (triggerDbfs);
        juce::AudioBuffer<float> buffer (1, static_cast<int> (subBlockSamples));
        TestHelpers::fillWithSine (buffer, testSampleRate, static_cast<double> (centreFrequencyHz), amplitude);
        return buffer;
    }

    // Feeds `band`'s detector a sustained tone at the band's own centre
    // frequency (locking its dynamic gain/Q at a steady-state value) for
    // `settleSubBlocks` sub-blocks, with a silent scratch main signal (this
    // settling phase produces no probe output of its own).
    void settleDynamicGain (DynamicBand& band, float triggerDbfs)
    {
        const auto triggerBuffer = makeTriggerBuffer (triggerDbfs);
        const juce::dsp::AudioBlock<const float> triggerBlock (triggerBuffer);

        juce::AudioBuffer<float> scratchMain (1, static_cast<int> (subBlockSamples));

        for (int i = 0; i < settleSubBlocks; ++i)
        {
            scratchMain.clear();
            juce::dsp::AudioBlock<float> mainBlock (scratchMain);
            band.processSubBlock (mainBlock, triggerBlock, 0, subBlockSamples);
        }
    }

    // Measures the magnitude response (dB, relative to the probe's own
    // input amplitude) at `probeFrequencyHz`, while the detector keeps
    // being fed the same locked trigger tone every sub-block - so the
    // dynamic gain/Q stays pinned at whatever settleDynamicGain() locked it
    // to, regardless of what the probe itself is doing.
    double measureProbeResponseDb (DynamicBand& band, float triggerDbfs, float probeFrequencyHz)
    {
        const auto triggerBuffer = makeTriggerBuffer (triggerDbfs);
        const juce::dsp::AudioBlock<const float> triggerBlock (triggerBuffer);

        juce::AudioBuffer<float> probeBuffer (1, probeSamples);
        TestHelpers::fillWithSine (probeBuffer, testSampleRate, static_cast<double> (probeFrequencyHz), 0.5f);

        juce::AudioBuffer<float> processed;
        processed.makeCopyOf (probeBuffer);

        int position = 0;

        while (position < probeSamples)
        {
            const auto n = juce::jmin (static_cast<int> (subBlockSamples), probeSamples - position);
            juce::dsp::AudioBlock<float> mainBlock (processed);
            auto subBlock = mainBlock.getSubBlock (static_cast<size_t> (position), static_cast<size_t> (n));
            band.processSubBlock (subBlock, triggerBlock, 0, static_cast<size_t> (n));
            position += n;
        }

        REQUIRE (TestHelpers::allSamplesFinite (processed));

        // Skip the first half of the buffer to let the biquad's own
        // transient response to the (possibly new) probe frequency settle
        // before measuring.
        const auto settleSamples = probeSamples / 2;
        const auto inRms = TestHelpers::tailRms (probeBuffer, settleSamples);
        const auto outRms = TestHelpers::tailRms (processed, settleSamples);
        REQUIRE (inRms > 0.0);

        return juce::Decibels::gainToDecibels (outRms / inRms);
    }

    // Finds the -3dB-equivalent crossing frequency on one side of centre:
    // the point where the response has recovered halfway (in dB) back
    // toward 0 dB from the centre frequency's own measured dip - a
    // monotonic, comparable bandwidth proxy (log-frequency bisection,
    // exploiting the response's monotonic approach back toward 0 dB moving
    // away from centre for a single Bell biquad within this search range).
    float findHalfwayCrossingHz (DynamicBand& band, float triggerDbfs, double halfwayDb, float outerMultiplier)
    {
        auto innerFreq = centreFrequencyHz;
        auto outerFreq = centreFrequencyHz * outerMultiplier;

        for (int iteration = 0; iteration < 12; ++iteration)
        {
            const auto midFreq = std::sqrt (innerFreq * outerFreq); // geometric mean - log-frequency midpoint
            const auto midDb = measureProbeResponseDb (band, triggerDbfs, midFreq);

            if (midDb <= halfwayDb) // still deep in the cut - crossing is further toward outer
                innerFreq = midFreq;
            else // already recovered past halfway - crossing is closer to centre
                outerFreq = midFreq;
        }

        return 0.5f * (innerFreq + outerFreq);
    }

    double measureMinus3dBBandwidthHz (DynamicBand& band, float triggerDbfs)
    {
        const auto centreDb = measureProbeResponseDb (band, triggerDbfs, centreFrequencyHz);
        REQUIRE (centreDb < -0.5); // must actually be a real, measurable cut to have a bandwidth at all

        const auto halfwayDb = centreDb * 0.5;

        const auto lowCrossingHz = findHalfwayCrossingHz (band, triggerDbfs, halfwayDb, 0.1f);
        const auto highCrossingHz = findHalfwayCrossingHz (band, triggerDbfs, halfwayDb, 10.0f);

        return static_cast<double> (highCrossingHz - lowCrossingHz);
    }
}

TEST_CASE ("Gain/Q coupling: measured -3 dB bandwidth is wider at near-full dynamic gain than at near-zero dynamic gain",
           "[dsp][gainq]")
{
    DynamicBand restBand (DynamicBand::ShelfDirection::none);
    restBand.prepare (makeSpec());
    configureBand (restBand);

    // (a) near-zero dynamic gain: trigger well below Threshold.
    settleDynamicGain (restBand, thresholdDb - 30.0f);
    const auto restBandwidthHz = measureMinus3dBBandwidthHz (restBand, thresholdDb - 30.0f);

    DynamicBand engagedBand (DynamicBand::ShelfDirection::none);
    engagedBand.prepare (makeSpec());
    configureBand (engagedBand);

    // (b) near-full dynamic gain: trigger well above Threshold, past the
    // knee, so the dynamic component saturates at (close to) |Range|.
    settleDynamicGain (engagedBand, thresholdDb + 30.0f);
    const auto engagedBandwidthHz = measureMinus3dBBandwidthHz (engagedBand, thresholdDb + 30.0f);

    INFO ("rest bandwidth (Hz) = " << restBandwidthHz);
    INFO ("engaged bandwidth (Hz) = " << engagedBandwidthHz);

    // Non-trivial margin: at least 40% wider, not just measurably different
    // - this plugin's gainQMinMultiplier (0.4, DynamicBand.h) widens Q by
    // up to 2.5x at full dynamic gain, so a real, substantial difference is
    // expected, not a rounding-error-sized one.
    CHECK (engagedBandwidthHz > restBandwidthHz * 1.4);
}

TEST_CASE ("Gain/Q coupling off: measured -3 dB bandwidth stays the same at near-zero and near-full dynamic gain",
           "[dsp][gainq]")
{
    DynamicBand restBand (DynamicBand::ShelfDirection::none);
    restBand.prepare (makeSpec());
    configureBand (restBand);
    restBand.setGainQ (false);

    settleDynamicGain (restBand, thresholdDb - 30.0f);
    const auto restBandwidthHz = measureMinus3dBBandwidthHz (restBand, thresholdDb - 30.0f);

    DynamicBand engagedBand (DynamicBand::ShelfDirection::none);
    engagedBand.prepare (makeSpec());
    configureBand (engagedBand);
    engagedBand.setGainQ (false);

    settleDynamicGain (engagedBand, thresholdDb + 30.0f);
    const auto engagedBandwidthHz = measureMinus3dBBandwidthHz (engagedBand, thresholdDb + 30.0f);

    INFO ("rest bandwidth (Hz) = " << restBandwidthHz);
    INFO ("engaged bandwidth (Hz) = " << engagedBandwidthHz);

    CHECK (engagedBandwidthHz == Catch::Approx (restBandwidthHz).margin (restBandwidthHz * 0.15));
}

//==============================================================================
// v0.4.0 (SOTA brief F6/T12): the coupling now reads a 30 ms-smoothed copy of
// the dynamic gain rather than the raw instantaneous value.
//
// Why that mattered: up to v0.3.0 the coupling's input was the gain
// computer's decision for the current sub-block, so a transient crossing the
// threshold could swing the realised Q across its entire range - baseQ down to
// 0.4*baseQ - in a single 32-sample step. At Q 12 that is a 7.2 Q-unit jump in
// 0.67 ms, i.e. the filter's whole shape changing between one sub-block and
// the next.
//
// The measurement reads the Q the band actually baked into its coefficients
// (DynamicBand::getLastAppliedFilterQ), so this is the realised trajectory
// rather than an argument about coefficients.
namespace
{
    // A trigger buffer long enough to hold a whole number of periods, fed to
    // the detector with a rolling offset. The 32-sample buffer the cases above
    // reuse from sample 0 every sub-block is only phase-continuous by
    // accident: repeating a 32-sample slice of a 1 kHz sine actually presents
    // the detector with a 1500 Hz-periodic waveform whose spectrum has no
    // 1 kHz component at all, which a narrow (Q >= 4) detector bandpass
    // rejects almost completely. These cases sweep Q up to 12, so they need a
    // genuinely continuous trigger.
    constexpr int continuousTriggerSamples = 4800; // exactly 100 periods of 1 kHz at 48 kHz

    juce::AudioBuffer<float> makeContinuousTrigger (float amplitude)
    {
        juce::AudioBuffer<float> buffer (1, continuousTriggerSamples);
        TestHelpers::fillWithSine (buffer, testSampleRate, static_cast<double> (centreFrequencyHz), amplitude);
        return buffer;
    }

    struct QTrajectory
    {
        double maxStepPerSubBlock = 0.0;
        float restQ = 0.0f;
        float engagedQ = 0.0f;
    };

    // Drives a band from rest to full Range with an abrupt burst and reports
    // both the largest realised-Q change between consecutive sub-blocks and
    // the endpoints of the traversal.
    QTrajectory measureQTrajectory (float nominalQ, float dialedRangeDb)
    {
        constexpr int restSubBlocks = 500;
        constexpr int burstSubBlocks = 4000; // ~2.7 s, far past the 30 ms smoother
        constexpr float loudAmplitude = 0.6f;

        DynamicBand band (DynamicBand::ShelfDirection::none);
        band.setOn (true);
        band.setFrequencyHz (centreFrequencyHz);
        band.setQ (nominalQ);
        band.setStaticGainDb (0.0f);
        band.setRangeDb (dialedRangeDb);
        // 30 dB of overshoot: the gain computer slams straight to the Range
        // clamp, which is the harshest input the coupling can be given.
        band.setThresholdDb (juce::Decibels::gainToDecibels (loudAmplitude) - 30.0f);
        band.setAttackMs (0.1f);
        band.setReleaseMs (50.0f);
        band.setGainQ (true);
        band.prepare (makeSpec());

        const auto quietTrigger = makeContinuousTrigger (loudAmplitude * 0.001f);
        const auto loudTrigger = makeContinuousTrigger (loudAmplitude);
        const juce::dsp::AudioBlock<const float> quietBlock (quietTrigger);
        const juce::dsp::AudioBlock<const float> loudBlock (loudTrigger);

        juce::AudioBuffer<float> scratch (1, static_cast<int> (subBlockSamples));

        size_t offset = 0;

        const auto advance = [&offset] ()
        {
            offset += subBlockSamples;

            if (offset + subBlockSamples > static_cast<size_t> (continuousTriggerSamples))
                offset = 0;
        };

        for (int i = 0; i < restSubBlocks; ++i)
        {
            scratch.clear();
            juce::dsp::AudioBlock<float> mainBlock (scratch);
            band.processSubBlock (mainBlock, quietBlock, offset, subBlockSamples);
            advance();
        }

        QTrajectory result;
        result.restQ = band.getLastAppliedFilterQ();

        auto previousQ = result.restQ;

        for (int i = 0; i < burstSubBlocks; ++i)
        {
            scratch.clear();
            juce::dsp::AudioBlock<float> mainBlock (scratch);
            band.processSubBlock (mainBlock, loudBlock, offset, subBlockSamples);
            advance();

            const auto currentQ = band.getLastAppliedFilterQ();
            result.maxStepPerSubBlock = juce::jmax (result.maxStepPerSubBlock,
                                                     static_cast<double> (std::abs (currentQ - previousQ)));
            previousQ = currentQ;
        }

        result.engagedQ = previousQ;
        return result;
    }
}

TEST_CASE ("Gain/Q coupling: the realised Q never moves more than 0.5 Q units in a single sub-block",
           "[dsp][gainq][smoothness]")
{
    for (const auto nominalQ : { 2.0f, 6.0f, 12.0f })
    {
        const auto trajectory = measureQTrajectory (nominalQ, -12.0f);

        INFO ("base Q = " << nominalQ << ", largest realised-Q step per sub-block = "
              << trajectory.maxStepPerSubBlock);
        CHECK (trajectory.maxStepPerSubBlock < 0.5);
    }
}

TEST_CASE ("Gain/Q coupling: the realised Q still traverses its full documented span", "[dsp][gainq][smoothness]")
{
    // The counterpart to the smoothness bound: the coupling must still get all
    // the way from baseQ to 0.4*baseQ, or the assertion above could be passed
    // by a coupling that had simply stopped working.
    constexpr float nominalQ = 6.0f;

    const auto trajectory = measureQTrajectory (nominalQ, -12.0f);

    INFO ("rest Q = " << trajectory.restQ << ", engaged Q = " << trajectory.engagedQ
          << " (expected ~" << nominalQ * 0.4f << ")");
    CHECK (trajectory.restQ == Catch::Approx (nominalQ).margin (0.01));
    CHECK (trajectory.engagedQ == Catch::Approx (nominalQ * 0.4f).margin (0.05));
}

TEST_CASE ("Gain/Q coupling off: the realised Q never moves at all", "[dsp][gainq][smoothness]")
{
    // Range 0 disengages the coupling just as switching gainQ off does.
    const auto trajectory = measureQTrajectory (6.0f, 0.0f);

    INFO ("largest realised-Q step per sub-block with the coupling disengaged = "
          << trajectory.maxStepPerSubBlock);
    CHECK (trajectory.maxStepPerSubBlock < 1.0e-6);
    CHECK (trajectory.engagedQ == Catch::Approx (6.0f).margin (0.01));
}

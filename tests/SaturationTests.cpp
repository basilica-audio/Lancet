#include "dsp/DynamicBand.h"
#include "TestHelpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstring>

// v0.3.0 (docs/voicing-notes.md) "gentle saturation stage on boosted
// bands": `bN_sat`, opt-in, off by default. When on, a
// std::tanh(x*drive)/drive waveshaper (DynamicBand::applySaturation()) is
// applied to a band's own post-filter samples, but ONLY while the band's
// combined (static + dynamic) applied gain is strictly positive (boosting)
// - a cutting or idle band is untouched even with `sat` on.
//
// Measurement technique: exercises DynamicBand directly with Range == 0 (a
// pure static-gain band), isolating the saturation stage from the gain
// computer entirely - `appliedGainDb` settles to a known, constant static
// Gain value almost immediately (only the 50 ms gain-smoothing ramp to wait
// out), so the tail of a processed probe sine is a clean measurement of the
// saturation stage alone. Distortion is measured without an FFT: a best-fit
// linear gain `a` is estimated by correlating the output against the input
// (a = <in,out> / <in,in>), then the residual `output - a*input` is what a
// perfectly linear gain stage would NOT produce - its RMS, normalised by
// the input's own RMS, is a real, FFT-free proxy for "how much non-linear
// (harmonic) energy did this stage add".
namespace
{
    constexpr double testSampleRate = 48000.0;
    constexpr float centreFrequencyHz = 1000.0f;
    constexpr size_t subBlockSamples = 32; // matches LancetEngine's own sub-block granularity
    constexpr int probeSamples = 16384;

    juce::dsp::ProcessSpec makeSpec()
    {
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = testSampleRate;
        spec.maximumBlockSize = static_cast<juce::uint32> (probeSamples);
        spec.numChannels = 1;
        return spec;
    }

    void configureBand (DynamicBand& band, float staticGainDb, bool saturation)
    {
        band.setOn (true);
        band.setFrequencyHz (centreFrequencyHz);
        band.setQ (1.0f);
        band.setStaticGainDb (staticGainDb);
        band.setRangeDb (0.0f); // no dynamics - isolates saturation from the gain computer entirely
        band.setThresholdDb (-30.0f);
        band.setAttackMs (5.0f);
        band.setReleaseMs (150.0f);
        band.setSaturation (saturation);
    }

    struct ProcessedProbe
    {
        juce::AudioBuffer<float> input;
        juce::AudioBuffer<float> output;
    };

    // Feeds `band` a full-scale-ish sine at its own centre frequency,
    // sub-block by sub-block (matching LancetEngine's own granularity), and
    // returns both the dry input and the processed output.
    ProcessedProbe processProbe (DynamicBand& band)
    {
        juce::AudioBuffer<float> probe (1, probeSamples);
        TestHelpers::fillWithSine (probe, testSampleRate, static_cast<double> (centreFrequencyHz), 0.5f);

        juce::AudioBuffer<float> processed;
        processed.makeCopyOf (probe);

        // A silent, sub-block-sized trigger buffer for the Detector - inert
        // here since Range == 0 makes the Detector's own measured level
        // unused (see DynamicBand::processSubBlock's `if (rangeDb != 0.0f)`
        // branch).
        juce::AudioBuffer<float> silentTrigger (1, static_cast<int> (subBlockSamples));
        silentTrigger.clear();
        const juce::dsp::AudioBlock<const float> triggerBlock (silentTrigger);

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
        return { probe, processed };
    }

    // Best-fit-linear-gain residual RMS, normalised by the input's own RMS -
    // see file comment for the full derivation. `startSample` skips the
    // gain-smoothing ramp's own settle time.
    double measureDistortionRatio (const juce::AudioBuffer<float>& input, const juce::AudioBuffer<float>& output, int startSample)
    {
        const auto* in = input.getReadPointer (0);
        const auto* out = output.getReadPointer (0);
        const auto numSamples = input.getNumSamples();

        double sumInIn = 0.0;
        double sumInOut = 0.0;

        for (int i = startSample; i < numSamples; ++i)
        {
            sumInIn += static_cast<double> (in[i]) * static_cast<double> (in[i]);
            sumInOut += static_cast<double> (in[i]) * static_cast<double> (out[i]);
        }

        REQUIRE (sumInIn > 0.0);
        const auto bestFitGain = sumInOut / sumInIn;

        double sumResidualSquared = 0.0;

        for (int i = startSample; i < numSamples; ++i)
        {
            const auto residual = static_cast<double> (out[i]) - bestFitGain * static_cast<double> (in[i]);
            sumResidualSquared += residual * residual;
        }

        const auto residualRms = std::sqrt (sumResidualSquared / static_cast<double> (numSamples - startSample));
        const auto inputRms = TestHelpers::tailRms (input, startSample);
        REQUIRE (inputRms > 0.0);

        return residualRms / inputRms;
    }

    // Skips the ~50 ms gain-smoothing ramp (DynamicBand::smoothingTimeSeconds)
    // plus margin, so measurement starts once the static gain has fully
    // settled.
    constexpr int measureStartSample = static_cast<int> (0.2 * testSampleRate);
}

TEST_CASE ("Saturation on a boosting band measurably adds harmonic/non-linear energy relative to the same "
           "boost with Saturation off",
           "[dsp][saturation]")
{
    DynamicBand bandSatOff (DynamicBand::ShelfDirection::none);
    bandSatOff.prepare (makeSpec());
    configureBand (bandSatOff, 9.0f, false);
    const auto satOffResult = processProbe (bandSatOff);
    const auto satOffDistortion = measureDistortionRatio (satOffResult.input, satOffResult.output, measureStartSample);

    DynamicBand bandSatOn (DynamicBand::ShelfDirection::none);
    bandSatOn.prepare (makeSpec());
    configureBand (bandSatOn, 9.0f, true);
    const auto satOnResult = processProbe (bandSatOn);
    const auto satOnDistortion = measureDistortionRatio (satOnResult.input, satOnResult.output, measureStartSample);

    INFO ("distortion ratio, saturation off = " << satOffDistortion);
    INFO ("distortion ratio, saturation on  = " << satOnDistortion);

    // A plain +9 dB linear gain stage should be very close to distortion-free.
    CHECK (satOffDistortion < 0.01);

    // Saturation must add a real, non-trivial amount of non-linear energy -
    // at least 5x the (near-zero) linear-stage baseline.
    CHECK (satOnDistortion > satOffDistortion * 5.0);
    CHECK (satOnDistortion > 0.01);
}

TEST_CASE ("Saturation is bypassed while a band is cutting, even with Saturation enabled", "[dsp][saturation]")
{
    DynamicBand cuttingSatOff (DynamicBand::ShelfDirection::none);
    cuttingSatOff.prepare (makeSpec());
    configureBand (cuttingSatOff, -9.0f, false);
    const auto cuttingSatOffResult = processProbe (cuttingSatOff);
    const auto cuttingSatOffDistortion = measureDistortionRatio (cuttingSatOffResult.input, cuttingSatOffResult.output, measureStartSample);

    DynamicBand cuttingSatOn (DynamicBand::ShelfDirection::none);
    cuttingSatOn.prepare (makeSpec());
    configureBand (cuttingSatOn, -9.0f, true);
    const auto cuttingSatOnResult = processProbe (cuttingSatOn);
    const auto cuttingSatOnDistortion = measureDistortionRatio (cuttingSatOnResult.input, cuttingSatOnResult.output, measureStartSample);

    INFO ("cutting distortion ratio, saturation off = " << cuttingSatOffDistortion);
    INFO ("cutting distortion ratio, saturation on  = " << cuttingSatOnDistortion);

    // "Boosted bands" scope: a cutting band must read (near-)identically
    // with Saturation on vs off - the waveshaper must never engage here.
    CHECK (cuttingSatOnDistortion == Catch::Approx (cuttingSatOffDistortion).margin (0.005));
    CHECK (cuttingSatOnDistortion < 0.01);
}

TEST_CASE ("Saturation is bypassed on an idle (0 dB Gain, 0 dB Range) band even with Saturation enabled",
           "[dsp][saturation]")
{
    DynamicBand idleBand (DynamicBand::ShelfDirection::none);
    idleBand.prepare (makeSpec());
    configureBand (idleBand, 0.0f, true);
    const auto result = processProbe (idleBand);

    // appliedGainDb == 0.0f exactly takes the true-bypass path (see
    // DynamicBand::processSubBlock's own comment on the exact-0-dB case) -
    // output must be bit-identical to input, not merely low-distortion.
    // std::memcmp (not a float ==) avoids an unhelpful -Wfloat-equal
    // warning for what is a deliberate, expected bit-identity check.
    CHECK (std::memcmp (result.output.getReadPointer (0),
                         result.input.getReadPointer (0),
                         static_cast<size_t> (result.input.getNumSamples()) * sizeof (float))
           == 0);
}

TEST_CASE ("Saturation produces finite output at the Gain/Range ceiling", "[dsp][saturation][robustness]")
{
    DynamicBand band (DynamicBand::ShelfDirection::none);
    band.prepare (makeSpec());
    configureBand (band, 12.0f, true); // +12 dB is this plugin's own static Gain ceiling
    const auto result = processProbe (band);

    CHECK (TestHelpers::allSamplesFinite (result.output));
    CHECK (TestHelpers::peakAbsolute (result.output) < 10.0f); // generously bounded, no blow-up
}

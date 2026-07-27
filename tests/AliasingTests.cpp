#include "dsp/AdaaTanh.h"
#include "dsp/DynamicBand.h"
#include "TestHelpers.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstring>

// v0.4.0 (SOTA brief F5, test T8): the per-band Saturation stage's memoryless
// tanh is replaced by a first-order antiderivative-antialiased (ADAA1) tanh
// of the same shape (src/dsp/AdaaTanh.h).
//
//==============================================================================
// What is - and is not - claimed
//
// The research this release draws on only ever *measured* an absolute alias
// floor for ADAA1 running at 2x oversampling, and rates plain 1x processing
// poorly. Lancet runs ADAA1 at base rate on purpose: the saturator is gentle
// (drive <= 2.5), it must stay at zero latency, and an oversampling stage
// would cost either latency or phase distortion for a stage that is off by
// default. So there is no validated absolute alias tier to assert against,
// and this file deliberately asserts none.
//
// What it asserts instead is *relative* and *level-pinned*: at each of three
// fixed input levels, the shipped ADAA1 kernel must put measurably less
// energy into the dominant fold-back line than the memoryless kernel it
// replaces, driven by the identical filtered signal at the identical drive.
//
// The 6 dB bound is derived, not aspirational. ADAA1 averages the
// nonlinearity along the straight line between consecutive samples, which is
// equivalent to a one-sample boxcar applied to the continuous-time distortion
// before sampling - so a harmonic at frequency f is attenuated by
// |sinc(f/fs)| before it folds. The stimulus is a 10 kHz tone at 48 kHz, whose
// dominant fold is the 3rd harmonic at 30 kHz landing on 18 kHz:
//
//     |sinc(30/48)| = sin(pi*0.625)/(pi*0.625) = 0.4705  ->  -6.55 dB
//
// Every higher harmonic is suppressed far more (the 5th, at 50 kHz, by about
// 28 dB), which is what the total-in-band-alias-power assertion picks up.
//
// The measured figures are printed unconditionally (WARN, so they reach CI
// logs) because the brief makes recording them a duty: any future absolute
// specification for this stage has to be calibrated from these numbers rather
// than guessed.
//
//==============================================================================
// Measurement technique
//
// No FFT and no window: the analysis length is chosen so that the stimulus
// and every fold-back line complete a whole number of periods in it, which
// makes a direct sin/cos correlation exact and leakage-free. At 48 kHz with a
// 10 kHz tone, 4800 samples is 1000 periods of the fundamental and 1800 of
// the 18 kHz alias line.
//
// tanh is an odd function, so the only true harmonic below Nyquist is the
// fundamental itself - every other in-band component is fold-back. "Total
// in-band alias power" is therefore just total power minus the fundamental's,
// with no harmonic bookkeeping needed.
namespace
{
    constexpr double testSampleRate = 48000.0;
    constexpr double stimulusHz = 10000.0;
    constexpr double dominantAliasHz = 18000.0; // 3rd harmonic (30 kHz) folded about Nyquist
    constexpr float bandGainDb = 12.0f;         // drives the saturator to its 2.5 ceiling
    constexpr size_t subBlockSamples = 32;

    constexpr int settleSamples = 12000; // 250 ms - far past the 15 ms static-gain smoother
    constexpr int analysisSamples = 4800;
    constexpr int totalSamples = settleSamples + analysisSamples;

    juce::dsp::ProcessSpec makeSpec (int numChannels)
    {
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = testSampleRate;
        spec.maximumBlockSize = static_cast<juce::uint32> (totalSamples);
        spec.numChannels = static_cast<juce::uint32> (numChannels);
        return spec;
    }

    void configureBand (DynamicBand& band, float staticGainDb, bool saturation)
    {
        band.setOn (true);
        band.setFrequencyHz (static_cast<float> (stimulusHz));
        band.setQ (1.0f);
        band.setStaticGainDb (staticGainDb);
        band.setRangeDb (0.0f); // static band: isolates the saturator from the gain computer
        band.setThresholdDb (-30.0f);
        band.setAttackMs (5.0f);
        band.setReleaseMs (150.0f);
        band.setSaturation (saturation);
    }

    // Feeds `input` through `band` in engine-sized sub-blocks, in place.
    void processThroughBand (DynamicBand& band, juce::AudioBuffer<float>& buffer)
    {
        juce::AudioBuffer<float> silentTrigger (buffer.getNumChannels(), static_cast<int> (subBlockSamples));
        silentTrigger.clear();
        const juce::dsp::AudioBlock<const float> triggerBlock (silentTrigger);

        int position = 0;

        while (position < buffer.getNumSamples())
        {
            const auto n = juce::jmin (static_cast<int> (subBlockSamples), buffer.getNumSamples() - position);
            juce::dsp::AudioBlock<float> mainBlock (buffer);
            auto subBlock = mainBlock.getSubBlock (static_cast<size_t> (position), static_cast<size_t> (n));
            band.processSubBlock (subBlock, triggerBlock, 0, static_cast<size_t> (n));
            position += n;
        }

        REQUIRE (TestHelpers::allSamplesFinite (buffer));
    }

    // The drive DynamicBand itself would use at this static gain - kept in
    // one place so the in-test memoryless reference cannot silently drift
    // away from the shipped mapping.
    float driveForGainDb (float gainDb)
    {
        constexpr float driveFloor = 0.3f;
        constexpr float driveCeiling = 2.5f;
        constexpr float gainReferenceDb = 12.0f;

        const auto fraction = juce::jlimit (0.0f, 1.0f, gainDb / gainReferenceDb);
        return juce::jmap (fraction, driveFloor, driveCeiling);
    }

    struct AliasMeasurement
    {
        double dominantAliasPower = 0.0;
        double totalAliasPower = 0.0;
        double fundamentalPower = 0.0;
    };

    AliasMeasurement measure (const juce::AudioBuffer<float>& buffer, int channel = 0)
    {
        AliasMeasurement result;

        const auto fundamental = TestHelpers::toneAmplitude (buffer, channel, settleSamples, analysisSamples,
                                                              stimulusHz, testSampleRate);
        const auto alias = TestHelpers::toneAmplitude (buffer, channel, settleSamples, analysisSamples,
                                                        dominantAliasHz, testSampleRate);

        result.fundamentalPower = TestHelpers::tonePower (fundamental);
        result.dominantAliasPower = TestHelpers::tonePower (alias);
        result.totalAliasPower = juce::jmax (0.0, TestHelpers::meanSquare (buffer, channel, settleSamples, analysisSamples)
                                                    - result.fundamentalPower);

        return result;
    }

    double powerRatioDb (double numerator, double denominator)
    {
        constexpr double floorPower = 1.0e-30;
        return 10.0 * std::log10 (juce::jmax (floorPower, numerator) / juce::jmax (floorPower, denominator));
    }

    struct KernelOutputs
    {
        juce::AudioBuffer<float> adaa;
        juce::AudioBuffer<float> memoryless;
    };

    // Runs one input level through the shipped band (ADAA on) and builds the
    // memoryless reference from the band's own filter-only output at the same
    // drive, so the two differ by the kernel and nothing else.
    KernelOutputs runBothKernels (float inputAmplitude)
    {
        juce::AudioBuffer<float> stimulus (1, totalSamples);
        TestHelpers::fillWithSine (stimulus, testSampleRate, stimulusHz, inputAmplitude);

        KernelOutputs outputs;

        outputs.adaa.makeCopyOf (stimulus);
        DynamicBand adaaBand (DynamicBand::ShelfDirection::none);
        configureBand (adaaBand, bandGainDb, true);
        adaaBand.prepare (makeSpec (1));
        processThroughBand (adaaBand, outputs.adaa);

        juce::AudioBuffer<float> filterOnly;
        filterOnly.makeCopyOf (stimulus);
        DynamicBand filterBand (DynamicBand::ShelfDirection::none);
        configureBand (filterBand, bandGainDb, false);
        filterBand.prepare (makeSpec (1));
        processThroughBand (filterBand, filterOnly);

        const auto drive = driveForGainDb (bandGainDb);

        outputs.memoryless.makeCopyOf (filterOnly);
        auto* data = outputs.memoryless.getWritePointer (0);

        for (int i = 0; i < totalSamples; ++i)
            data[i] = std::tanh (data[i] * drive) / drive;

        return outputs;
    }
}

TEST_CASE ("ADAA saturation puts at least 6 dB less energy into the dominant fold-back line than the memoryless "
           "kernel, at every pinned input level",
           "[dsp][saturation][aliasing]")
{
    for (const auto levelDbfs : { -24.0f, -12.0f, -6.0f })
    {
        const auto amplitude = juce::Decibels::decibelsToGain (levelDbfs);
        const auto outputs = runBothKernels (amplitude);

        const auto adaa = measure (outputs.adaa);
        const auto memoryless = measure (outputs.memoryless);

        const auto improvementDb = powerRatioDb (memoryless.dominantAliasPower, adaa.dominantAliasPower);
        const auto adaaAliasToSignalDb = powerRatioDb (adaa.dominantAliasPower, adaa.fundamentalPower);
        const auto memorylessAliasToSignalDb = powerRatioDb (memoryless.dominantAliasPower, memoryless.fundamentalPower);

        // Recorded on purpose (SOTA brief T8c): any future absolute
        // specification for this stage must be calibrated from these numbers.
        WARN ("T8 dominant fold (18 kHz), input " << levelDbfs << " dBFS: "
              << "ADAA alias/signal = " << adaaAliasToSignalDb << " dB, "
              << "memoryless alias/signal = " << memorylessAliasToSignalDb << " dB, "
              << "improvement = " << improvementDb << " dB");

        INFO ("input level = " << levelDbfs << " dBFS");
        CHECK (improvementDb >= 6.0);
    }
}

TEST_CASE ("ADAA saturation never increases total in-band alias power relative to the memoryless kernel",
           "[dsp][saturation][aliasing]")
{
    for (const auto levelDbfs : { -24.0f, -12.0f, -6.0f })
    {
        const auto amplitude = juce::Decibels::decibelsToGain (levelDbfs);
        const auto outputs = runBothKernels (amplitude);

        const auto adaa = measure (outputs.adaa);
        const auto memoryless = measure (outputs.memoryless);

        const auto totalImprovementDb = powerRatioDb (memoryless.totalAliasPower, adaa.totalAliasPower);

        WARN ("T8 total in-band alias power, input " << levelDbfs << " dBFS: "
              << "ADAA = " << powerRatioDb (adaa.totalAliasPower, adaa.fundamentalPower) << " dB rel. signal, "
              << "memoryless = " << powerRatioDb (memoryless.totalAliasPower, memoryless.fundamentalPower)
              << " dB rel. signal, improvement = " << totalImprovementDb << " dB");

        INFO ("input level = " << levelDbfs << " dBFS");
        CHECK (totalImprovementDb >= 0.0);
    }
}

TEST_CASE ("Saturation disabled leaves the filter-only path bit-identical, even on a band whose ADAA state is "
           "being kept warm",
           "[dsp][saturation][aliasing]")
{
    // T8(e). A cutting band with Saturation *enabled* still bypasses the
    // waveshaper (the stage is scoped to boosts), but it does keep the ADAA
    // kernel's previous-sample state current so that engaging saturation
    // mid-stream is continuous. That bookkeeping must not touch the audio: the
    // output has to match a band with Saturation switched off bit for bit.
    const auto amplitude = juce::Decibels::decibelsToGain (-12.0f);

    juce::AudioBuffer<float> stimulus (1, totalSamples);
    TestHelpers::fillWithSine (stimulus, testSampleRate, stimulusHz, amplitude);

    juce::AudioBuffer<float> satOff;
    satOff.makeCopyOf (stimulus);
    DynamicBand offBand (DynamicBand::ShelfDirection::none);
    configureBand (offBand, -bandGainDb, false);
    offBand.prepare (makeSpec (1));
    processThroughBand (offBand, satOff);

    juce::AudioBuffer<float> satOnCutting;
    satOnCutting.makeCopyOf (stimulus);
    DynamicBand onBand (DynamicBand::ShelfDirection::none);
    configureBand (onBand, -bandGainDb, true);
    onBand.prepare (makeSpec (1));
    processThroughBand (onBand, satOnCutting);

    CHECK (std::memcmp (satOff.getReadPointer (0),
                         satOnCutting.getReadPointer (0),
                         static_cast<size_t> (totalSamples) * sizeof (float))
           == 0);
}

TEST_CASE ("ADAA saturation keeps one kernel state per channel: stereo output matches the same signals run as mono",
           "[dsp][saturation][aliasing]")
{
    // T8(f). DynamicBand processes a band's channels in separate passes, so a
    // single shared previous-sample value would make each channel's first
    // difference quotient span the *other* channel's last sample - broadband
    // garbage rather than saturation, and only in stereo. Two deliberately
    // different tones make that failure mode unmissable.
    const auto amplitude = juce::Decibels::decibelsToGain (-12.0f);
    constexpr double rightHz = 9700.0;

    juce::AudioBuffer<float> stereo (2, totalSamples);

    for (int i = 0; i < totalSamples; ++i)
    {
        const auto leftPhase = juce::MathConstants<double>::twoPi * stimulusHz * i / testSampleRate;
        const auto rightPhase = juce::MathConstants<double>::twoPi * rightHz * i / testSampleRate;
        stereo.setSample (0, i, amplitude * static_cast<float> (std::sin (leftPhase)));
        stereo.setSample (1, i, amplitude * static_cast<float> (std::sin (rightPhase)));
    }

    juce::AudioBuffer<float> monoLeft (1, totalSamples);
    juce::AudioBuffer<float> monoRight (1, totalSamples);
    monoLeft.copyFrom (0, 0, stereo, 0, 0, totalSamples);
    monoRight.copyFrom (0, 0, stereo, 1, 0, totalSamples);

    DynamicBand stereoBand (DynamicBand::ShelfDirection::none);
    configureBand (stereoBand, bandGainDb, true);
    stereoBand.prepare (makeSpec (2));
    processThroughBand (stereoBand, stereo);

    DynamicBand leftBand (DynamicBand::ShelfDirection::none);
    configureBand (leftBand, bandGainDb, true);
    leftBand.prepare (makeSpec (1));
    processThroughBand (leftBand, monoLeft);

    DynamicBand rightBand (DynamicBand::ShelfDirection::none);
    configureBand (rightBand, bandGainDb, true);
    rightBand.prepare (makeSpec (1));
    processThroughBand (rightBand, monoRight);

    CHECK (std::memcmp (stereo.getReadPointer (0), monoLeft.getReadPointer (0),
                         static_cast<size_t> (totalSamples) * sizeof (float))
           == 0);
    CHECK (std::memcmp (stereo.getReadPointer (1), monoRight.getReadPointer (0),
                         static_cast<size_t> (totalSamples) * sizeof (float))
           == 0);
}

TEST_CASE ("AdaaTanh: the difference-quotient fallback matches the quotient it replaces at the crossover",
           "[dsp][saturation][aliasing]")
{
    // Unit-level guard for the ill-conditioned branch in AdaaTanh::process:
    // just above the threshold the kernel uses (F0(x0)-F0(x1))/(x0-x1), just
    // below it uses tanh((x0+x1)/2). The two must agree to well within
    // single-precision audio resolution, or the crossover would be audible as
    // a tiny discontinuity whenever a signal loitered near it.
    for (const auto centre : { -2.0, -0.5, 0.0, 0.5, 2.0 })
    {
        const auto halfDelta = 1.0e-4;
        const auto x0 = centre + halfDelta;
        const auto x1 = centre - halfDelta;

        const auto quotient = (lnct::AdaaTanh::antiderivative (x0) - lnct::AdaaTanh::antiderivative (x1)) / (x0 - x1);
        const auto midpoint = std::tanh (0.5 * (x0 + x1));

        INFO ("centre = " << centre << ", quotient = " << quotient << ", midpoint = " << midpoint);
        CHECK (std::abs (quotient - midpoint) < 1.0e-6);
    }
}

TEST_CASE ("AdaaTanh: ln(cosh(x)) stays finite at magnitudes that would overflow the naive form",
           "[dsp][saturation][aliasing][robustness]")
{
    // cosh(800) overflows a double; the |x| + log1p(exp(-2|x|)) - ln(2)
    // identity does not. A band at maximum drive can never reach these
    // magnitudes, but a NaN/Inf sweep can.
    for (const auto x : { 0.0, 1.0, 50.0, 800.0, -800.0, 1.0e6, -1.0e6 })
    {
        const auto value = lnct::AdaaTanh::antiderivative (x);
        INFO ("x = " << x << ", ln(cosh(x)) = " << value);
        CHECK (std::isfinite (value));
    }

    // Correctness where the naive form still works.
    for (const auto x : { -3.0, -0.25, 0.0, 0.25, 3.0 })
        CHECK (std::abs (lnct::AdaaTanh::antiderivative (x) - std::log (std::cosh (x))) < 1.0e-12);
}

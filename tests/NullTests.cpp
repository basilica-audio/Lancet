#include "dsp/DynamicBand.h"
#include "dsp/LancetEngine.h"
#include "TestHelpers.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <random>

// Guarantee #1 (docs/design-brief.md): "all bands off (or on with gain=0,
// range=0) -> bit-transparent apart from trim (assert <= -120 dBFS diff at
// unity trim)."
namespace
{
    constexpr double testSampleRate = 48000.0;
    constexpr int testBlockSize = 8192;
    constexpr int settleSamples = 2048;

    // Deviation (dB) of `processed` from `reference`, expressed relative to
    // the reference signal's own level - the same relative-deviation
    // convention sibling plugins' flat-sum null tests use (e.g. triptych's
    // tests/EngineTests.cpp).
    double measureDeviationDb (const juce::AudioBuffer<float>& reference, const juce::AudioBuffer<float>& processed)
    {
        double sumOfSquaresRef = 0.0;
        double sumOfSquaresDiff = 0.0;
        int counted = 0;

        for (int channel = 0; channel < reference.getNumChannels(); ++channel)
        {
            const auto* refData = reference.getReadPointer (channel);
            const auto* procData = processed.getReadPointer (channel);

            for (int i = settleSamples; i < reference.getNumSamples(); ++i)
            {
                const auto refValue = static_cast<double> (refData[i]);
                const auto diff = static_cast<double> (procData[i]) - refValue;

                sumOfSquaresRef += refValue * refValue;
                sumOfSquaresDiff += diff * diff;
                ++counted;
            }
        }

        REQUIRE (counted > 0);

        const auto refRms = std::sqrt (sumOfSquaresRef / static_cast<double> (counted));
        const auto diffRms = std::sqrt (sumOfSquaresDiff / static_cast<double> (counted));

        REQUIRE (refRms > 0.0);

        // Explicit, far-below-the-guarantee floor: juce::Decibels::
        // gainToDecibels's default floor is only -100 dB, which would
        // silently clamp (and hide) a genuinely near-bit-exact result well
        // past this test's own -120 dBFS bar.
        return juce::Decibels::gainToDecibels (diffRms / refRms, -300.0);
    }

    juce::dsp::ProcessSpec makeTestSpec (int numChannels)
    {
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = testSampleRate;
        spec.maximumBlockSize = static_cast<juce::uint32> (testBlockSize);
        spec.numChannels = static_cast<juce::uint32> (numChannels);
        return spec;
    }

    // Design-brief default frequencies per band (docs/design-brief.md).
    constexpr float defaultFreqHz[LancetEngine::numBands] = { 100.0f, 250.0f, 630.0f, 1600.0f, 4000.0f, 10000.0f };
}

TEST_CASE ("Null: all bands off is bit-transparent apart from trim (<= -120 dBFS)", "[dsp][null]")
{
    static constexpr double probeFrequenciesHz[] = { 40.0, 150.0, 500.0, 1500.0, 5000.0, 15000.0 };

    for (const auto probeHz : probeFrequenciesHz)
    {
        INFO ("probe frequency = " << probeHz << " Hz");

        LancetEngine engine;
        engine.setInputTrimDb (0.0f);
        engine.setOutputTrimDb (0.0f);
        engine.setMixPercent (100.0f);
        // Every band defaults to off (LancetEngine/DynamicBand's own
        // built-in defaults - see DynamicBand.h) - no per-band setters
        // called here on purpose.

        engine.prepare (makeTestSpec (2));

        juce::AudioBuffer<float> reference (2, testBlockSize);
        TestHelpers::fillWithSine (reference, testSampleRate, probeHz, 0.7f);

        juce::AudioBuffer<float> processed;
        processed.makeCopyOf (reference);

        juce::dsp::AudioBlock<float> block (processed);
        engine.process (block);

        const auto deviationDb = measureDeviationDb (reference, processed);
        CHECK (deviationDb <= -120.0);
    }
}

TEST_CASE ("Null: all bands on with Gain=0/Range=0 is bit-transparent apart from trim (<= -120 dBFS)", "[dsp][null]")
{
    static constexpr double probeFrequenciesHz[] = { 40.0, 150.0, 500.0, 1500.0, 5000.0, 15000.0 };

    for (const auto probeHz : probeFrequenciesHz)
    {
        INFO ("probe frequency = " << probeHz << " Hz");

        LancetEngine engine;
        engine.setInputTrimDb (0.0f);
        engine.setOutputTrimDb (0.0f);
        engine.setMixPercent (100.0f);

        for (int band = 0; band < LancetEngine::numBands; ++band)
        {
            engine.setBandOn (band, true);
            engine.setBandFrequencyHz (band, defaultFreqHz[band]);
            engine.setBandQ (band, 1.0f);
            engine.setBandGainDb (band, 0.0f);
            engine.setBandRangeDb (band, 0.0f);
            engine.setBandThresholdDb (band, -30.0f);
        }

        engine.prepare (makeTestSpec (2));

        juce::AudioBuffer<float> reference (2, testBlockSize);
        TestHelpers::fillWithSine (reference, testSampleRate, probeHz, 0.7f);

        juce::AudioBuffer<float> processed;
        processed.makeCopyOf (reference);

        juce::dsp::AudioBlock<float> block (processed);
        engine.process (block);

        const auto deviationDb = measureDeviationDb (reference, processed);
        CHECK (deviationDb <= -120.0);
    }
}

//==============================================================================
// v0.4.0 (SOTA brief T2): the band core changed from an RBJ biquad to a
// trapezoidal state-variable filter (src/dsp/TptSvf.h). The two realise the
// same transfer function - the derivation is written out in TptSvf.h - and
// this is the numerical proof, so that "existing static EQ settings keep
// exactly the curve they had" is a measurement rather than an assertion.
//
// The reference below is an independent RBJ implementation in this file: the
// cookbook coefficients, evaluated in double precision through a transposed
// direct form II recursion. Double precision is deliberate. Comparing against
// a *float* TDF2 would measure the reference's own rounding error as much as
// the filter under test, and TDF2 at 100 Hz / 48 kHz is exactly where that
// error is worst; a double reference is the harder target and attributes any
// residual to the SVF alone.
namespace
{
    // One RBJ biquad, double precision, transposed direct form II.
    struct ReferenceBiquad
    {
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
        double z1 = 0.0, z2 = 0.0;

        void setPeaking (double sampleRate, double frequencyHz, double q, double gainDb)
        {
            const auto a = std::pow (10.0, gainDb / 40.0);
            const auto w0 = 2.0 * juce::MathConstants<double>::pi * frequencyHz / sampleRate;
            const auto alpha = std::sin (w0) / (2.0 * q);
            const auto cosW0 = std::cos (w0);

            normalise (1.0 + alpha * a, -2.0 * cosW0, 1.0 - alpha * a,
                        1.0 + alpha / a, -2.0 * cosW0, 1.0 - alpha / a);
        }

        void setShelf (double sampleRate, double frequencyHz, double q, double gainDb, bool isLowShelf)
        {
            const auto a = std::pow (10.0, gainDb / 40.0);
            const auto w0 = 2.0 * juce::MathConstants<double>::pi * frequencyHz / sampleRate;
            const auto alpha = std::sin (w0) / (2.0 * q);
            const auto cosW0 = std::cos (w0);
            const auto twoSqrtAAlpha = 2.0 * std::sqrt (a) * alpha;

            if (isLowShelf)
                normalise (a * ((a + 1.0) - (a - 1.0) * cosW0 + twoSqrtAAlpha),
                            2.0 * a * ((a - 1.0) - (a + 1.0) * cosW0),
                            a * ((a + 1.0) - (a - 1.0) * cosW0 - twoSqrtAAlpha),
                            (a + 1.0) + (a - 1.0) * cosW0 + twoSqrtAAlpha,
                            -2.0 * ((a - 1.0) + (a + 1.0) * cosW0),
                            (a + 1.0) + (a - 1.0) * cosW0 - twoSqrtAAlpha);
            else
                normalise (a * ((a + 1.0) + (a - 1.0) * cosW0 + twoSqrtAAlpha),
                            -2.0 * a * ((a - 1.0) + (a + 1.0) * cosW0),
                            a * ((a + 1.0) + (a - 1.0) * cosW0 - twoSqrtAAlpha),
                            (a + 1.0) - (a - 1.0) * cosW0 + twoSqrtAAlpha,
                            2.0 * ((a - 1.0) - (a + 1.0) * cosW0),
                            (a + 1.0) - (a - 1.0) * cosW0 - twoSqrtAAlpha);
        }

        double processSample (double x) noexcept
        {
            const auto y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }

    private:
        void normalise (double rawB0, double rawB1, double rawB2, double rawA0, double rawA1, double rawA2)
        {
            b0 = rawB0 / rawA0;
            b1 = rawB1 / rawA0;
            b2 = rawB2 / rawA0;
            a1 = rawA1 / rawA0;
            a2 = rawA2 / rawA0;
        }
    };

    // Pink-ish broadband noise: white noise through a one-pole tilt. Any
    // full-band excitation works here; the point is to exercise the whole
    // response rather than a single tone.
    void fillWithPinkNoise (juce::AudioBuffer<float>& buffer, unsigned int seed)
    {
        std::mt19937 rng (seed);
        std::uniform_real_distribution<float> distribution (-1.0f, 1.0f);

        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            auto* data = buffer.getWritePointer (channel);
            float state = 0.0f;

            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                const auto white = distribution (rng);
                state = 0.95f * state + 0.05f * white;
                data[i] = 0.25f * (white * 0.4f + state * 3.0f);
            }
        }
    }

    struct StaticBandSetting
    {
        float frequencyHz;
        float q;
        float gainDb;
        DynamicBand::ShelfDirection shelfDirection;
        bool shelfSelected;
    };
}

TEST_CASE ("Null: the TPT SVF band core matches an independent double-precision RBJ reference to better than "
           "-100 dBFS on broadband noise",
           "[dsp][null][svf]")
{
    // 10 s of noise at 48 kHz, per band setting: long enough that a slow
    // numerical divergence (the failure mode a short buffer would miss) has
    // time to show up.
    constexpr int noiseSamples = static_cast<int> (10.0 * testSampleRate);
    constexpr size_t subBlockSamples = 32;

    static const StaticBandSetting settings[] = {
        { 100.0f, 0.9f, 6.0f, DynamicBand::ShelfDirection::none, false },
        { 100.0f, 0.9f, -6.0f, DynamicBand::ShelfDirection::none, false },
        { 1000.0f, 1.0f, 6.0f, DynamicBand::ShelfDirection::none, false },
        { 1000.0f, 8.0f, -12.0f, DynamicBand::ShelfDirection::none, false },
        { 8000.0f, 3.0f, 9.0f, DynamicBand::ShelfDirection::none, false },
        { 40.0f, 12.0f, 12.0f, DynamicBand::ShelfDirection::none, false }, // deliberately the worst-conditioned corner
        { 150.0f, 1.0f, 9.0f, DynamicBand::ShelfDirection::low, true },
        { 150.0f, 1.0f, -9.0f, DynamicBand::ShelfDirection::low, true },
        { 6000.0f, 1.0f, 9.0f, DynamicBand::ShelfDirection::high, true },
        { 6000.0f, 1.0f, -9.0f, DynamicBand::ShelfDirection::high, true },
    };

    juce::AudioBuffer<float> noise (1, noiseSamples);
    fillWithPinkNoise (noise, 20260725u);

    for (const auto& setting : settings)
    {
        INFO ("freq = " << setting.frequencyHz << " Hz, Q = " << setting.q << ", gain = " << setting.gainDb
              << " dB, shelf = " << (setting.shelfSelected ? "yes" : "no"));

        juce::AudioBuffer<float> processed;
        processed.makeCopyOf (noise);

        DynamicBand band (setting.shelfDirection);
        band.setOn (true);
        band.setShelfSelected (setting.shelfSelected);
        band.setFrequencyHz (setting.frequencyHz);
        band.setQ (setting.q);
        band.setStaticGainDb (setting.gainDb);
        band.setRangeDb (0.0f); // static: no dynamics in this comparison at all
        band.setThresholdDb (-30.0f);

        juce::dsp::ProcessSpec spec;
        spec.sampleRate = testSampleRate;
        spec.maximumBlockSize = static_cast<juce::uint32> (noiseSamples);
        spec.numChannels = 1;
        band.prepare (spec);

        juce::AudioBuffer<float> trigger (1, static_cast<int> (subBlockSamples));
        trigger.clear();
        const juce::dsp::AudioBlock<const float> triggerBlock (trigger);

        for (int position = 0; position < noiseSamples; position += static_cast<int> (subBlockSamples))
        {
            const auto n = juce::jmin (static_cast<int> (subBlockSamples), noiseSamples - position);
            juce::dsp::AudioBlock<float> mainBlock (processed);
            auto subBlock = mainBlock.getSubBlock (static_cast<size_t> (position), static_cast<size_t> (n));
            band.processSubBlock (subBlock, triggerBlock, 0, static_cast<size_t> (n));
        }

        ReferenceBiquad reference;

        if (setting.shelfSelected)
            reference.setShelf (testSampleRate, setting.frequencyHz, 0.70710678, setting.gainDb,
                                 setting.shelfDirection == DynamicBand::ShelfDirection::low);
        else
            reference.setPeaking (testSampleRate, setting.frequencyHz, setting.q, setting.gainDb);

        double peakResidual = 0.0;

        // Skip the first 4096 samples: the two structures start from
        // different (both zero) internal states but converge on the same
        // response, and the static-gain smoother needs its 15 ms.
        for (int i = 0; i < noiseSamples; ++i)
        {
            const auto expected = reference.processSample (static_cast<double> (noise.getSample (0, i)));

            if (i < 4096)
                continue;

            peakResidual = juce::jmax (peakResidual, std::abs (static_cast<double> (processed.getSample (0, i)) - expected));
        }

        const auto residualDb = juce::Decibels::gainToDecibels (peakResidual, -300.0);
        INFO ("peak residual = " << residualDb << " dBFS");
        CHECK (residualDb <= -100.0);
    }
}

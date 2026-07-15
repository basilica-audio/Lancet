#include "dsp/LancetEngine.h"
#include "TestHelpers.h"

#include <catch2/catch_test_macros.hpp>

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

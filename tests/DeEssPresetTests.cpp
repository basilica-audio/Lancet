#include "PluginProcessor.h"
#include "params/ParameterIds.h"
#include "presets/PresetManager.h"
#include "TestHelpers.h"

#include <BinaryData.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <random>

// Guarantee #7 (docs/design-brief.md §4): "De-essing preset spectral proof"
// - process an identical sibilant-vowel test signal (high-frequency energy
// concentrated 4-9 kHz) through the "De-Ess Stack" preset and assert
// measurable sibilance-band energy reduction during the sibilant segment
// relative to the same signal with the preset's dynamic bands disabled - a
// real measured-spectrum proof that the shipped preset actually performs
// the documented function, not just that it loads.
namespace
{
    constexpr double testSampleRate = 48000.0;
    constexpr int vowelSegmentSamples = static_cast<int> (0.3 * testSampleRate);
    constexpr int sibilantSegmentSamples = static_cast<int> (0.3 * testSampleRate);

    // Synthesizes a stereo "sibilant vowel" test signal: a low-frequency
    // harmonic complex (the "vowel") throughout, with a broadband
    // 4-9 kHz-filtered noise burst added during the second half (the
    // "sibilant" segment) - high-frequency energy concentrated exactly in
    // the region the design brief calls for. Both channels carry identical
    // content (this plugin's default bus layout is stereo).
    juce::AudioBuffer<float> buildSibilantVowelSignal()
    {
        const auto totalSamples = vowelSegmentSamples + sibilantSegmentSamples;
        juce::AudioBuffer<float> buffer (2, totalSamples);

        std::mt19937 rng (42);
        std::uniform_real_distribution<float> noiseDist (-1.0f, 1.0f);

        auto sibilanceBandpassCoeffs = juce::dsp::IIR::Coefficients<float>::makeBandPass (testSampleRate, 6000.0f, 0.9f);
        juce::dsp::IIR::Filter<float> sibilanceShaper (sibilanceBandpassCoeffs);
        sibilanceShaper.prepare (juce::dsp::ProcessSpec { testSampleRate, static_cast<juce::uint32> (totalSamples), 1 });

        constexpr float vowelFundamentalHz = 150.0f;
        constexpr float vowelAmplitude = 0.3f;
        constexpr float sibilanceAmplitude = 0.5f;

        auto* left = buffer.getWritePointer (0);
        auto* right = buffer.getWritePointer (1);

        for (int i = 0; i < totalSamples; ++i)
        {
            const auto t = static_cast<double> (i) / testSampleRate;
            float vowel = 0.0f;

            for (int harmonic = 1; harmonic <= 4; ++harmonic)
            {
                const auto phase = juce::MathConstants<double>::twoPi * vowelFundamentalHz * harmonic * t;
                vowel += (vowelAmplitude / static_cast<float> (harmonic)) * static_cast<float> (std::sin (phase));
            }

            float sample = vowel;

            if (i >= vowelSegmentSamples)
            {
                const auto shapedNoise = sibilanceShaper.processSample (noiseDist (rng));
                sample += shapedNoise * sibilanceAmplitude;
            }
            else
            {
                // Keep the shaper's own filter state warm (fed silence)
                // during the vowel-only segment, so it ramps in cleanly
                // rather than starting cold exactly at the segment boundary.
                sibilanceShaper.processSample (0.0f);
            }

            left[i] = sample;
            right[i] = sample;
        }

        return buffer;
    }

    // Measures RMS energy in the 4-9 kHz sibilance band (channel 0) over
    // [startSample, buffer.getNumSamples()) by running the buffer through a
    // dedicated measurement bandpass (independent of Lancet's own
    // processing) and computing its output RMS.
    double measureSibilanceBandRms (const juce::AudioBuffer<float>& buffer, int startSample)
    {
        auto coeffs = juce::dsp::IIR::Coefficients<float>::makeBandPass (testSampleRate, 6000.0f, 0.9f);
        juce::dsp::IIR::Filter<float> measurementFilter (coeffs);
        measurementFilter.prepare (juce::dsp::ProcessSpec { testSampleRate, static_cast<juce::uint32> (buffer.getNumSamples()), 1 });

        juce::AudioBuffer<float> filtered (1, buffer.getNumSamples());
        filtered.copyFrom (0, 0, buffer, 0, 0, buffer.getNumSamples());
        auto* data = filtered.getWritePointer (0);

        for (int i = 0; i < filtered.getNumSamples(); ++i)
            data[i] = measurementFilter.processSample (data[i]);

        return TestHelpers::tailRms (filtered, startSample);
    }

    std::vector<basilica::presets::FactoryPresetAsset> makeFactoryAssets()
    {
        return {
            { BinaryData::default_json, BinaryData::default_jsonSize },
            { BinaryData::gentleGlue_json, BinaryData::gentleGlue_jsonSize },
            { BinaryData::deEssStack_json, BinaryData::deEssStack_jsonSize },
            { BinaryData::transientSnareCrack_json, BinaryData::transientSnareCrack_jsonSize },
            { BinaryData::mixBussSettle_json, BinaryData::mixBussSettle_jsonSize },
            { BinaryData::slowTonalRide_json, BinaryData::slowTonalRide_jsonSize },
            { BinaryData::chestResonanceTamer_json, BinaryData::chestResonanceTamer_jsonSize },
            { BinaryData::fastRecoveryDemo_json, BinaryData::fastRecoveryDemo_jsonSize },
            { BinaryData::listenCheck_json, BinaryData::listenCheck_jsonSize },
            { BinaryData::analogWarmthLift_json, BinaryData::analogWarmthLift_jsonSize },
        };
    }

    // Loads the "De-Ess Stack" factory preset onto `processor` via a fresh,
    // isolated PresetManager, then (if `disableDynamics`) zeroes both of
    // its engaged bands' Range so their dynamic component is inert (a true
    // bypass at Gain=0 - see DynamicBand.cpp) while every other parameter
    // (frequency, Q, static Gain, on/off) stays exactly as the preset
    // shipped it - the "same preset with its dynamic bands disabled"
    // baseline this guarantee compares against.
    void loadDeEssStackPreset (LancetAudioProcessor& processor, const juce::File& scratchDir, bool disableDynamics)
    {
        basilica::presets::PresetManagerConfig config;
        config.pluginId = "com.yvesvogl.lancet";
        config.pluginName = "Lancet";
        config.manufacturerName = "Yves Vogl";
        config.pluginVersion = "0.2.0-test";
        config.userPresetsDirectoryOverrideForTests = scratchDir;

        basilica::presets::PresetManager manager (processor.apvts, config, makeFactoryAssets());
        REQUIRE (manager.loadPreset ("De-Ess Stack"));

        if (disableDynamics)
        {
            auto* b5Range = processor.apvts.getParameter (ParamIDs::b5Range);
            auto* b6Range = processor.apvts.getParameter (ParamIDs::b6Range);
            REQUIRE (b5Range != nullptr);
            REQUIRE (b6Range != nullptr);
            b5Range->setValueNotifyingHost (b5Range->convertTo0to1 (0.0f));
            b6Range->setValueNotifyingHost (b6Range->convertTo0to1 (0.0f));
        }
    }
}

TEST_CASE ("De-Ess Stack preset: measurably reduces 4-9 kHz sibilance-band energy relative to the same preset "
           "with its dynamic bands disabled",
           "[presets][deess]")
{
    const auto drySignal = buildSibilantVowelSignal();

    juce::AudioBuffer<float> withDynamics;
    withDynamics.makeCopyOf (drySignal);

    juce::AudioBuffer<float> withoutDynamics;
    withoutDynamics.makeCopyOf (drySignal);

    const auto scratchDirRoot = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                     .getChildFile ("LancetDeEssPresetTests")
                                     .getChildFile (juce::String (juce::Time::getHighResolutionTicks()));

    {
        LancetAudioProcessor processor;
        processor.prepareToPlay (testSampleRate, drySignal.getNumSamples());
        loadDeEssStackPreset (processor, scratchDirRoot.getChildFile ("with"), false);

        juce::MidiBuffer midi;
        processor.processBlock (withDynamics, midi);
    }

    {
        LancetAudioProcessor processor;
        processor.prepareToPlay (testSampleRate, drySignal.getNumSamples());
        loadDeEssStackPreset (processor, scratchDirRoot.getChildFile ("without"), true);

        juce::MidiBuffer midi;
        processor.processBlock (withoutDynamics, midi);
    }

    scratchDirRoot.deleteRecursively();

    REQUIRE (TestHelpers::allSamplesFinite (withDynamics));
    REQUIRE (TestHelpers::allSamplesFinite (withoutDynamics));

    // Measure only within the sibilant segment (the second half), skipping
    // its own first few ms so the detector's Attack has time to engage.
    const auto measureStart = vowelSegmentSamples + static_cast<int> (0.02 * testSampleRate);

    const auto sibilanceRmsWithDynamics = measureSibilanceBandRms (withDynamics, measureStart);
    const auto sibilanceRmsWithoutDynamics = measureSibilanceBandRms (withoutDynamics, measureStart);

    INFO ("sibilance RMS with dynamics = " << sibilanceRmsWithDynamics);
    INFO ("sibilance RMS without dynamics (baseline) = " << sibilanceRmsWithoutDynamics);

    REQUIRE (sibilanceRmsWithoutDynamics > 0.0);

    const auto reductionDb = juce::Decibels::gainToDecibels (sibilanceRmsWithDynamics / sibilanceRmsWithoutDynamics);

    // A real, measurable reduction - not just "different" (De-Ess Stack's
    // Band 5 alone is an -8 dB Range cutting node, so a comfortably-past-
    // noise-floor reduction is expected once it engages).
    CHECK (reductionDb < -1.0);
}

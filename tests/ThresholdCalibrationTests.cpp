#include "PluginProcessor.h"
#include "dsp/Detector.h"
#include "dsp/LancetEngine.h"
#include "params/ParameterIds.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <juce_dsp/juce_dsp.h>

#include <cmath>
#include <complex>
#include <vector>

// Issue #4 threshold-calibration pass (docs/voicing-notes.md, "Threshold
// calibration" section): each band's default Threshold is defined as that
// band's own measured detector level under a fixed programme-level anchor -
// band-limited (20 Hz - 20 kHz) pink noise at -18 dBFS RMS - so that every
// band begins engaging at the same programme loudness out of the box.
//
// Why this is measurable at all: the detector's Split-mode bandpass is
// constant-Q (its relative bandwidth does not depend on centre frequency),
// and pink noise carries equal energy per octave, so the in-band level any
// band's detector settles at under the anchor is approximately the same
// (~= -24 dBFS) regardless of where on the frequency ladder the band sits.
// The v0.3.0 defaults (-28 dB .. -20 dB) were chosen without this
// measurement ("spread evenly between -20 and -28" per that release's own
// honesty section) and therefore produced the opposite of their intent -
// see docs/voicing-notes.md for the full old-vs-measured-vs-new table.
//
// These tests re-measure the real shipped Detector against the real shipped
// defaults (read from a fresh processor's APVTS, not hand-copied constants)
// - so the calibration is a frozen, regression-tested property of the
// plugin, not a one-off design-time claim.
namespace
{
    constexpr float anchorRmsDbFS = -18.0f;
    constexpr juce::int64 pinkSeed = 0x4c414e43; // "LANC"

    // Band-limited (20 Hz - 20 kHz) pink noise, synthesised in the frequency
    // domain: |X(f)| ~ 1/sqrt(f) inside the audible band, zero outside,
    // uniformly random phases from a fixed seed, then normalised to exactly
    // the requested RMS. Synthesising in the frequency domain (rather than
    // filtering white noise through e.g. the Kellet pinking filter, whose
    // coefficients are tuned for one specific sample rate) keeps the
    // spectrum exactly pink at every sample rate this file measures at.
    juce::AudioBuffer<float> makePinkNoise (double sampleRate, int fftOrder, float targetRmsDbFS)
    {
        const int n = 1 << fftOrder;
        juce::dsp::FFT fft (fftOrder);

        std::vector<juce::dsp::Complex<float>> spectrum (static_cast<size_t> (n), { 0.0f, 0.0f });
        std::vector<juce::dsp::Complex<float>> time (static_cast<size_t> (n));

        juce::Random random (pinkSeed);
        const auto binWidthHz = sampleRate / n;

        for (int k = 1; k < n / 2; ++k)
        {
            const auto frequencyHz = k * binWidthHz;
            if (frequencyHz < 20.0 || frequencyHz > 20000.0)
                continue;

            const auto magnitude = 1.0 / std::sqrt (frequencyHz); // power ~ 1/f
            const auto phase = juce::MathConstants<double>::twoPi * random.nextDouble();
            const std::complex<double> bin = std::polar (magnitude, phase);

            spectrum[static_cast<size_t> (k)] = { static_cast<float> (bin.real()), static_cast<float> (bin.imag()) };
            spectrum[static_cast<size_t> (n - k)] = { static_cast<float> (bin.real()), static_cast<float> (-bin.imag()) };
        }

        fft.perform (spectrum.data(), time.data(), true);

        juce::AudioBuffer<float> buffer (1, n);
        auto* data = buffer.getWritePointer (0);
        for (int i = 0; i < n; ++i)
            data[i] = time[static_cast<size_t> (i)].real();

        double sumSquares = 0.0;
        for (int i = 0; i < n; ++i)
            sumSquares += static_cast<double> (data[i]) * data[i];

        const auto rms = std::sqrt (sumSquares / n);
        REQUIRE (rms > 0.0);

        const auto targetRms = std::pow (10.0, static_cast<double> (targetRmsDbFS) / 20.0);
        buffer.applyGain (static_cast<float> (targetRms / rms));
        return buffer;
    }

    float readDefault (juce::AudioProcessorValueTreeState& apvts, const char* id)
    {
        auto* param = apvts.getParameter (id);
        REQUIRE (param != nullptr);
        return param->convertFrom0to1 (param->getDefaultValue());
    }

    struct BandParameterIds
    {
        const char* freq;
        const char* q;
        const char* attack;
        const char* release;
        const char* threshold;
    };

    constexpr BandParameterIds bandIds[] = {
        { ParamIDs::b1Freq, ParamIDs::b1Q, ParamIDs::b1Attack, ParamIDs::b1Release, ParamIDs::b1Threshold },
        { ParamIDs::b2Freq, ParamIDs::b2Q, ParamIDs::b2Attack, ParamIDs::b2Release, ParamIDs::b2Threshold },
        { ParamIDs::b3Freq, ParamIDs::b3Q, ParamIDs::b3Attack, ParamIDs::b3Release, ParamIDs::b3Threshold },
        { ParamIDs::b4Freq, ParamIDs::b4Q, ParamIDs::b4Attack, ParamIDs::b4Release, ParamIDs::b4Threshold },
        { ParamIDs::b5Freq, ParamIDs::b5Q, ParamIDs::b5Attack, ParamIDs::b5Release, ParamIDs::b5Threshold },
        { ParamIDs::b6Freq, ParamIDs::b6Q, ParamIDs::b6Attack, ParamIDs::b6Release, ParamIDs::b6Threshold },
    };

    // Feeds the anchor noise through an isolated Detector configured with
    // one band's shipped defaults and returns the mean settled envelope
    // level in dBFS (first second discarded as settle time).
    float measureBandDetectorLevelDb (juce::AudioProcessorValueTreeState& apvts,
                                       const BandParameterIds& ids,
                                       const juce::AudioBuffer<float>& noise,
                                       double sampleRate)
    {
        const auto totalSamples = noise.getNumSamples();

        Detector detector;
        // The listen buffer is sized to (and indexed within) maximumBlockSize,
        // so the spec must cover the whole buffer when feeding it as one
        // logical block in sub-block strides.
        detector.prepare ({ sampleRate, static_cast<juce::uint32> (totalSamples), 1 });
        detector.setFrequencyAndQ (readDefault (apvts, ids.freq), readDefault (apvts, ids.q));
        detector.setAttackMs (readDefault (apvts, ids.attack));
        detector.setReleaseMs (readDefault (apvts, ids.release));

        const juce::dsp::AudioBlock<const float> block (noise);
        const int blockSize = 512;
        const auto settleSamples = static_cast<int> (sampleRate);

        double sumDb = 0.0;
        int count = 0;

        for (int start = 0; start + blockSize <= totalSamples; start += blockSize)
        {
            const auto levelDb = detector.processSubBlock (block, static_cast<size_t> (start), static_cast<size_t> (blockSize));

            if (start >= settleSamples)
            {
                sumDb += levelDb;
                ++count;
            }
        }

        REQUIRE (count > 0);
        return static_cast<float> (sumDb / count);
    }
}

TEST_CASE ("Threshold calibration: every band's default Threshold matches its own measured detector level "
           "under the -18 dBFS RMS pink-noise anchor",
           "[dsp][voicing][threshold-calibration]")
{
    LancetAudioProcessor processor;
    auto& apvts = processor.apvts;

    // 2^19 samples ~= 11.9 s at 44.1k / 10.9 s at 48k; 2^20 keeps ~10.9 s at
    // 96k. Margins: the calibration was performed at 48 kHz and rounded to
    // whole dB, so 1.0 dB covers rounding plus realisation variance there;
    // 96 kHz gets 1.5 dB (the 10 kHz band's bilinear-transform warp differs
    // most between rates - measured 0.7 dB shift, see docs/voicing-notes.md).
    struct RateSetup { double sampleRate; int fftOrder; float marginDb; };
    constexpr RateSetup rates[] = {
        { 44100.0, 19, 1.0f },
        { 48000.0, 19, 1.0f },
        { 96000.0, 20, 1.5f },
    };

    for (const auto& rate : rates)
    {
        const auto noise = makePinkNoise (rate.sampleRate, rate.fftOrder, anchorRmsDbFS);

        int bandNumber = 1;
        for (const auto& ids : bandIds)
        {
            const auto measuredDb = measureBandDetectorLevelDb (apvts, ids, noise, rate.sampleRate);
            const auto thresholdDb = readDefault (apvts, ids.threshold);

            INFO ("sampleRate=" << rate.sampleRate << " band=" << bandNumber
                                 << " measured=" << measuredDb << " dBFS, default threshold=" << thresholdDb << " dB");
            CHECK (measuredDb == Catch::Approx (thresholdDb).margin (rate.marginDb));
            ++bandNumber;
        }
    }
}

TEST_CASE ("Threshold calibration: the onset gap (measured anchor level minus default Threshold) is uniform "
           "across all six bands",
           "[dsp][voicing][threshold-calibration]")
{
    LancetAudioProcessor processor;
    auto& apvts = processor.apvts;

    constexpr double sampleRate = 48000.0;
    const auto noise = makePinkNoise (sampleRate, 19, anchorRmsDbFS);

    float minGapDb = 1000.0f;
    float maxGapDb = -1000.0f;

    for (const auto& ids : bandIds)
    {
        const auto gapDb = measureBandDetectorLevelDb (apvts, ids, noise, sampleRate)
                            - readDefault (apvts, ids.threshold);
        minGapDb = juce::jmin (minGapDb, gapDb);
        maxGapDb = juce::jmax (maxGapDb, gapDb);
    }

    INFO ("onset gap spread across bands: [" << minGapDb << ", " << maxGapDb << "] dB");

    // The v0.3.0 defaults spread this gap across 7 dB (Band 2 at +3.2 dB vs
    // Band 6 at -4.3 dB); the calibrated defaults keep every band within a
    // narrow, common window around zero.
    CHECK (maxGapDb - minGapDb < 1.5f);
    CHECK (std::abs (minGapDb) < 1.0f);
    CHECK (std::abs (maxGapDb) < 1.0f);
}

TEST_CASE ("Threshold calibration: a default band with Range dialed in engages gently at the anchor level "
           "and stays idle 12 dB below it",
           "[dsp][voicing][threshold-calibration]")
{
    LancetAudioProcessor processor;
    auto& apvts = processor.apvts;

    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 512;
    constexpr int band3 = 2; // 0-based engine index

    const auto anchorNoise = makePinkNoise (sampleRate, 19, anchorRmsDbFS);

    // Band 3 (the default-on demo band) at its shipped defaults, with the
    // design brief's own sourced starting Range of -6 dB dialed in - the
    // canonical "first move" a user makes.
    const auto configureEngine = [&] (LancetEngine& engine)
    {
        engine.prepare ({ sampleRate, static_cast<juce::uint32> (blockSize), 1 });

        for (int band = 0; band < LancetEngine::numBands; ++band)
            engine.setBandOn (band, band == band3);

        engine.setBandFrequencyHz (band3, readDefault (apvts, ParamIDs::b3Freq));
        engine.setBandQ (band3, readDefault (apvts, ParamIDs::b3Q));
        engine.setBandGainDb (band3, 0.0f);
        engine.setBandRangeDb (band3, -6.0f);
        engine.setBandThresholdDb (band3, readDefault (apvts, ParamIDs::b3Threshold));
        engine.setBandAttackMs (band3, readDefault (apvts, ParamIDs::b3Attack));
        engine.setBandReleaseMs (band3, readDefault (apvts, ParamIDs::b3Release));
    };

    const auto measureMeanDynamicGainDb = [&] (float inputGainDb)
    {
        LancetEngine engine;
        configureEngine (engine);

        juce::AudioBuffer<float> scaled (1, anchorNoise.getNumSamples());
        scaled.copyFrom (0, 0, anchorNoise, 0, 0, anchorNoise.getNumSamples());
        scaled.applyGain (juce::Decibels::decibelsToGain (inputGainDb));

        const auto settleSamples = static_cast<int> (sampleRate);
        double sumDb = 0.0;
        int count = 0;

        juce::AudioBuffer<float> work (1, blockSize);

        for (int start = 0; start + blockSize <= scaled.getNumSamples(); start += blockSize)
        {
            work.copyFrom (0, 0, scaled, 0, start, blockSize);
            juce::dsp::AudioBlock<float> block (work);
            engine.process (block);

            if (start >= settleSamples)
            {
                sumDb += engine.getLastAppliedDynamicGainDb (band3);
                ++count;
            }
        }

        REQUIRE (count > 0);
        return sumDb / count;
    };

    // At the anchor the envelope hovers around the calibrated threshold, so
    // the soft knee (clamp(|-6| * 0.5, 2, 10) = 3 dB wide) produces a gentle
    // partial engagement - clearly active, nowhere near the -6 dB Range rail.
    const auto atAnchorDb = measureMeanDynamicGainDb (0.0f);
    INFO ("mean dynamic gain at anchor level: " << atAnchorDb << " dB");
    CHECK (atAnchorDb < -0.1);
    CHECK (atAnchorDb > -3.0);

    // 12 dB below the anchor (quiet material) the same band applies
    // essentially no dynamic gain: 2 * overshoot is far below the knee's
    // lower edge, so the gain computer's output is exactly zero once the
    // envelope has settled.
    const auto quietDb = measureMeanDynamicGainDb (-12.0f);
    INFO ("mean dynamic gain 12 dB below anchor: " << quietDb << " dB");
    CHECK (std::abs (quietDb) < 0.05);
}

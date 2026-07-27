#include "PluginProcessor.h"
#include "params/ParameterIds.h"
#include "TestHelpers.h"

#include <catch2/catch_test_macros.hpp>

// Guarantee #10 (docs/design-brief.md): "Automation smoothness: full-range
// gain sweep over 1 s produces no sample-to-sample jump > 3 dB (zipper
// guard)."
//
// Deliberately adversarial: rather than many small per-host-block
// automation steps (which would already be smooth regardless of the
// plugin's own internal smoothing, since the *host's* automation
// granularity would dominate), each test applies the *entire* gain range in
// a single abrupt setValueNotifyingHost() call partway through a
// continuous 1-second buffer, stressing whatever internal smoothing (see
// DynamicBand's gainSmoothed / juce::dsp::Gain's own ramp) is actually
// responsible for the guarantee.
//
// Bound derivation: a 0.7-amplitude, 1 kHz sine at 48 kHz has a natural
// per-sample slope of amplitude * 2*pi*f/fs =~ 0.092 even with *no* gain
// change at all. A literal, un-smoothed 3 dB gain pop landing exactly at
// the sine's peak would additionally displace that sample by
// amplitude * (10^(3/20) - 1) =~ 0.289. The threshold below sums both
// (with a small margin) - well under the >= 10x larger jump an actually
// unsmoothed *full-range* (24 dB) pop would produce (~10.4), so this test
// has real bug-catching power against a regression that removed smoothing.
namespace
{
    constexpr double testSampleRate = 48000.0;
    constexpr float testAmplitude = 0.7f;
    const float maxAllowedJump = testAmplitude * (2.0f * juce::MathConstants<float>::pi * 1000.0f / static_cast<float> (testSampleRate))
                                  + testAmplitude * (juce::Decibels::decibelsToGain (3.0f) - 1.0f) + 0.02f;

    // Processes 1 second of continuous 1 kHz sine through `processor`,
    // jumping `param` from normalised 0.0 to 1.0 in a single abrupt call
    // exactly halfway through, and returns the full buffer for inspection.
    juce::AudioBuffer<float> runAbruptFullRangeJump (LancetAudioProcessor& processor, juce::RangedAudioParameter& param)
    {
        constexpr int blockSize = 512;
        constexpr int totalSamples = static_cast<int> (testSampleRate); // 1 second
        constexpr int jumpAtSample = totalSamples / 2;

        param.setValueNotifyingHost (0.0f);

        juce::AudioBuffer<float> fullOutput (1, totalSamples);
        juce::MidiBuffer midi;

        int position = 0;
        bool jumped = false;

        while (position < totalSamples)
        {
            if (! jumped && position >= jumpAtSample)
            {
                param.setValueNotifyingHost (1.0f);
                jumped = true;
            }

            const auto thisBlockSize = juce::jmin (blockSize, totalSamples - position);

            juce::AudioBuffer<float> block (1, thisBlockSize);
            TestHelpers::fillWithSine (block, testSampleRate, 1000.0, testAmplitude, position);

            processor.processBlock (block, midi);

            fullOutput.copyFrom (0, position, block, 0, 0, thisBlockSize);

            position += thisBlockSize;
        }

        return fullOutput;
    }
}

TEST_CASE ("Zipper guard: an abrupt full-range Band Gain jump never produces a >3 dB-equivalent sample-to-sample step",
           "[dsp][zipper]")
{
    LancetAudioProcessor processor;
    processor.prepareToPlay (testSampleRate, 512);

    auto* onParam = processor.apvts.getParameter (ParamIDs::b3On);
    REQUIRE (onParam != nullptr);
    onParam->setValueNotifyingHost (1.0f);

    // Band 3's Freq defaults to 630 Hz, close enough to the 1 kHz probe
    // tone that the boost is substantially applied (not near a null).
    auto* freqParam = processor.apvts.getParameter (ParamIDs::b3Freq);
    REQUIRE (freqParam != nullptr);
    freqParam->setValueNotifyingHost (freqParam->convertTo0to1 (1000.0f));

    auto* gainParam = processor.apvts.getParameter (ParamIDs::b3Gain);
    REQUIRE (gainParam != nullptr);

    const auto output = runAbruptFullRangeJump (processor, *gainParam);

    REQUIRE (TestHelpers::allSamplesFinite (output));
    CHECK (TestHelpers::maxSampleToSampleJump (output) < maxAllowedJump);
}

TEST_CASE ("Zipper guard: an abrupt full-range Output Trim jump never produces a >3 dB-equivalent sample-to-sample step",
           "[dsp][zipper]")
{
    LancetAudioProcessor processor;
    processor.prepareToPlay (testSampleRate, 512);

    auto* outputTrimParam = processor.apvts.getParameter (ParamIDs::outTrim);
    REQUIRE (outputTrimParam != nullptr);

    const auto output = runAbruptFullRangeJump (processor, *outputTrimParam);

    REQUIRE (TestHelpers::allSamplesFinite (output));
    CHECK (TestHelpers::maxSampleToSampleJump (output) < maxAllowedJump);
}

//==============================================================================
// v0.4.0 (SOTA brief T5): the modulation guarantee the F1/F2 rework has to
// keep. v0.3.0 realised a band's gain by rebuilding RBJ coefficients once per
// 32-sample sub-block from a 50 ms-smoothed value; v0.4.0 removes the smoother
// and modulates a trapezoidal SVF per sample instead. The smoother was what
// used to keep the stepped path inaudible, so its removal has to be shown not
// to have re-introduced the steps it was hiding.
//
// How that is measured, and why not with an absolute sideband floor:
//
// The stimulus the brief nominates - a square-AM tone driving full-Range
// pumping - inherently produces a wide sideband skirt. An abruptly-switched
// amplitude puts energy at f0 +- n*4 Hz falling off only as 1/n, so at
// 1500 Hz away from the carrier the *stimulus itself* still carries roughly
// -50 dB of legitimate AM sidebands. An absolute "sidebands beyond the AM
// skirt <= -60 dBFS" bound would therefore fail a perfectly clean
// implementation. The skirt is signal, not artefact.
//
// A stepped-gain path does not raise that smooth skirt - it adds *discrete
// lines* on top of it, at the sub-block rate and its multiples (48 kHz / 32 =
// 1500 Hz), because that is the rate at which the gain would be quantised in
// time. So the assertions below are:
//
//   1. no discrete line at f0 +- 1500 Hz standing above its own neighbours -
//      i.e. above the strongest ordinary sideband within +-300 Hz of it. That
//      is exactly the "beyond the expected AM skirt" the brief is describing,
//      with "the skirt" read as the sideband structure that is actually
//      there rather than as a smooth floor; and
//   2. the RMS sample-to-sample discontinuity at sub-block boundaries is no
//      larger than everywhere else. A stepped implementation concentrates its
//      discontinuities on the boundaries by construction; a per-sample one
//      spreads them evenly. This is the sharpest available test for "no
//      per-sub-block stepped gain path exists" and needs no spectrum at all.
namespace
{
    constexpr size_t engineSubBlockSamples = 32;
    constexpr double subBlockRateHz = testSampleRate / static_cast<double> (engineSubBlockSamples);

    struct PumpingSetup
    {
        int bandIndex;
        bool shelfSelected;
        float frequencyHz;
        float rangeDb;
        float attackMs;
        double toneHz;
    };

    // Renders a 4 Hz square-amplitude-modulated tone through one band driven
    // to full Range, and returns the output.
    juce::AudioBuffer<float> renderSquareAmPumping (const PumpingSetup& setup, int numSamples)
    {
        constexpr float loudAmplitude = 0.6f;
        constexpr float quietAmplitude = 0.6f * 0.01f; // 40 dB below - well under threshold
        constexpr double modulationHz = 4.0;

        LancetEngine engine;
        engine.setBandOn (setup.bandIndex, true);
        engine.setBandShelfSelected (setup.bandIndex, setup.shelfSelected);
        engine.setBandFrequencyHz (setup.bandIndex, setup.frequencyHz);
        engine.setBandQ (setup.bandIndex, 1.0f);
        engine.setBandGainDb (setup.bandIndex, 0.0f);
        engine.setBandRangeDb (setup.bandIndex, setup.rangeDb);
        // 20 dB under the loud level: the gain computer saturates at the Range
        // clamp on every loud half-cycle, i.e. full-depth pumping.
        engine.setBandThresholdDb (setup.bandIndex, juce::Decibels::gainToDecibels (loudAmplitude) - 20.0f);
        engine.setBandAttackMs (setup.bandIndex, setup.attackMs);
        engine.setBandReleaseMs (setup.bandIndex, 40.0f);
        engine.setInputTrimDb (0.0f);
        engine.setOutputTrimDb (0.0f);
        engine.setMixPercent (100.0f);

        juce::dsp::ProcessSpec spec;
        spec.sampleRate = testSampleRate;
        spec.maximumBlockSize = static_cast<juce::uint32> (numSamples);
        spec.numChannels = 1;
        engine.prepare (spec);

        juce::AudioBuffer<float> buffer (1, numSamples);
        auto* data = buffer.getWritePointer (0);

        for (int i = 0; i < numSamples; ++i)
        {
            const auto modulationPhase = modulationHz * static_cast<double> (i) / testSampleRate;
            const auto loud = (modulationPhase - std::floor (modulationPhase)) < 0.5;
            const auto amplitude = loud ? loudAmplitude : quietAmplitude;

            const auto phase = juce::MathConstants<double>::twoPi * setup.toneHz * static_cast<double> (i) / testSampleRate;
            data[i] = amplitude * static_cast<float> (std::sin (phase));
        }

        juce::dsp::AudioBlock<float> block (buffer);
        engine.process (block);

        return buffer;
    }

    // Ratio (dB) of the magnitude at the sub-block-rate sideband to the local
    // skirt floor around it. A stepped gain path shows up as a line here.
    double measureSubBlockLineExcessDb (const juce::AudioBuffer<float>& output, double carrierHz, int fftOrder)
    {
        const auto fftSize = 1 << fftOrder;
        const auto analysisStart = output.getNumSamples() - fftSize;
        REQUIRE (analysisStart >= 0);

        const auto spectrum = TestHelpers::magnitudeSpectrum (output, 0, analysisStart, fftOrder);
        const auto binsPerHz = static_cast<double> (fftSize) / testSampleRate;

        const auto lineBin = static_cast<int> (std::lround ((carrierHz + subBlockRateHz) * binsPerHz));
        const auto neighbourhood = static_cast<int> (std::lround (300.0 * binsPerHz));

        // Peak within +-2 bins of the nominal line, so a fractional-bin
        // placement cannot hide a real line.
        double linePeak = 0.0;

        for (int bin = lineBin - 2; bin <= lineBin + 2; ++bin)
            if (bin >= 0 && bin < static_cast<int> (spectrum.size()))
                linePeak = juce::jmax (linePeak, static_cast<double> (spectrum[static_cast<size_t> (bin)]));

        // Compared against the strongest ordinary sideband within +-300 Hz,
        // excluding the line's own +-8 bins - see
        // TestHelpers::peakMagnitudeExcluding for why a peak and not a median.
        const auto neighbourPeak = TestHelpers::peakMagnitudeExcluding (spectrum,
                                                                         lineBin - neighbourhood,
                                                                         lineBin + neighbourhood,
                                                                         lineBin,
                                                                         8);

        REQUIRE (neighbourPeak > 0.0);
        return juce::Decibels::gainToDecibels (linePeak / neighbourPeak, -300.0);
    }

    void checkNoStepAtSubBlockBoundaries (const juce::AudioBuffer<float>& output, int analysisStart)
    {
        const auto boundaryRms = TestHelpers::jumpRms (output, 0, analysisStart,
                                                        [] (int i) { return (static_cast<size_t> (i) % engineSubBlockSamples) == 0; });
        const auto interiorRms = TestHelpers::jumpRms (output, 0, analysisStart,
                                                        [] (int i) { return (static_cast<size_t> (i) % engineSubBlockSamples) != 0; });

        REQUIRE (interiorRms > 0.0);

        INFO ("sub-block-boundary jump RMS = " << boundaryRms << ", interior jump RMS = " << interiorRms
              << ", ratio = " << (boundaryRms / interiorRms));

        // Statistically the two must be the same. 1.25 leaves room for the
        // sampling noise of a 1-in-32 subset without leaving room for an
        // actual step: quantising a 12 dB pumping gain to 32-sample updates
        // puts several times the interior figure on the boundaries.
        CHECK (boundaryRms <= interiorRms * 1.25);
    }
}

TEST_CASE ("Zipper guard: full-Range bell pumping produces no discrete line at the sub-block rate",
           "[dsp][zipper][modulation]")
{
    constexpr int fftOrder = 15; // 32768 samples ~ 1.46 Hz resolution at 48 kHz
    constexpr int numSamples = 48000 * 2;
    constexpr double toneHz = 1000.0;

    const auto output = renderSquareAmPumping ({ 2, false, 1000.0f, -12.0f, 0.1f, toneHz }, numSamples);
    REQUIRE (TestHelpers::allSamplesFinite (output));

    const auto excessDb = measureSubBlockLineExcessDb (output, toneHz, fftOrder);
    INFO ("sub-block-rate sideband stands " << excessDb << " dB above its strongest neighbouring sideband");
    CHECK (excessDb < 3.0);

    checkNoStepAtSubBlockBoundaries (output, 24000);
}

TEST_CASE ("Zipper guard: full-Range High Shelf pumping (the De-Ess Stack configuration) produces no discrete line "
           "at the sub-block rate",
           "[dsp][zipper][modulation]")
{
    // T5(c). Shelves take exactly the same per-sample gain path as bells in
    // v0.4.0 - the shipped De-Ess Stack preset drives Band 6 as a dynamic
    // shelf, so a stepped-shelf fallback would have been an audible regression
    // rather than a theoretical one.
    constexpr int fftOrder = 15;
    constexpr int numSamples = 48000 * 2;
    constexpr double toneHz = 9000.0;

    const auto output = renderSquareAmPumping ({ 5, true, 8000.0f, -12.0f, 0.1f, toneHz }, numSamples);
    REQUIRE (TestHelpers::allSamplesFinite (output));

    const auto excessDb = measureSubBlockLineExcessDb (output, toneHz, fftOrder);
    INFO ("sub-block-rate sideband stands " << excessDb << " dB above its strongest neighbouring sideband");
    CHECK (excessDb < 3.0);

    checkNoStepAtSubBlockBoundaries (output, 24000);
}

TEST_CASE ("Zipper guard: full-Range Low Shelf boost pumping produces no discrete line at the sub-block rate",
           "[dsp][zipper][modulation]")
{
    constexpr int fftOrder = 15;
    constexpr int numSamples = 48000 * 2;
    constexpr double toneHz = 120.0;

    const auto output = renderSquareAmPumping ({ 0, true, 200.0f, 12.0f, 0.1f, toneHz }, numSamples);
    REQUIRE (TestHelpers::allSamplesFinite (output));

    const auto excessDb = measureSubBlockLineExcessDb (output, toneHz, fftOrder);
    INFO ("sub-block-rate sideband stands " << excessDb << " dB above its strongest neighbouring sideband");
    CHECK (excessDb < 3.0);

    checkNoStepAtSubBlockBoundaries (output, 24000);
}

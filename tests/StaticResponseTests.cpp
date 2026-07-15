#include "dsp/LancetEngine.h"
#include "TestHelpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

// Guarantee #2 (docs/design-brief.md): "band boost/cut magnitude at centre
// frequency within +-0.5 dB of the analytic RBJ response for representative
// (freq, Q, gain) triples." TestHelpers::rbjPeakMagnitudeDb/
// rbjShelfMagnitudeDb are independent re-implementations of the RBJ "Audio
// EQ Cookbook" formulas (the same formulas juce::dsp::IIR::ArrayCoefficients
// implements) - this validates that LancetEngine wires frequency/Q/gain
// into the right filter with the right sign and scale, not that JUCE's own
// filter math is correct.
namespace
{
    constexpr double testSampleRate = 48000.0;
    constexpr int testBlockSize = 32768;
    constexpr int settleSamples = 16384; // >> the 50 ms gain-smoothing ramp and the biquad's own transient

    static constexpr float shelfQ = 0.70710678f;

    // Measures the settled steady-state gain (dB) LancetEngine applies to a
    // single-tone probe, with exactly one band on, static (Range == 0), and
    // every trim/mix at unity/neutral.
    float measureBandGainDb (int bandIndex, bool shelfSelected, float freqHz, float q, float gainDb, double probeFrequencyHz)
    {
        LancetEngine engine;
        engine.setBandOn (bandIndex, true);
        engine.setBandShelfSelected (bandIndex, shelfSelected);
        engine.setBandFrequencyHz (bandIndex, freqHz);
        engine.setBandQ (bandIndex, q);
        engine.setBandGainDb (bandIndex, gainDb);
        engine.setBandRangeDb (bandIndex, 0.0f);
        engine.setInputTrimDb (0.0f);
        engine.setOutputTrimDb (0.0f);
        engine.setMixPercent (100.0f);

        juce::dsp::ProcessSpec spec;
        spec.sampleRate = testSampleRate;
        spec.maximumBlockSize = static_cast<juce::uint32> (testBlockSize);
        spec.numChannels = 1;
        engine.prepare (spec);

        juce::AudioBuffer<float> reference (1, testBlockSize);
        TestHelpers::fillWithSine (reference, testSampleRate, probeFrequencyHz, 0.5f);

        juce::AudioBuffer<float> processed;
        processed.makeCopyOf (reference);

        juce::dsp::AudioBlock<float> block (processed);
        engine.process (block);

        const auto inRms = TestHelpers::tailRms (reference, settleSamples);
        const auto outRms = TestHelpers::tailRms (processed, settleSamples);

        REQUIRE (inRms > 0.0);

        return static_cast<float> (juce::Decibels::gainToDecibels (outRms / inRms));
    }
}

TEST_CASE ("Bell band: magnitude at centre frequency matches the analytic RBJ peaking-EQ response within +-0.5 dB",
           "[dsp][static-response]")
{
    struct Triple
    {
        float freqHz;
        float q;
        float gainDb;
    };

    // A representative spread of frequency/Q/gain, spanning both boost and
    // cut, low and high Q, and low/mid/high frequency - using Band 3 (index
    // 2), which is always Bell (no Type parameter - see ParameterIds.h).
    static constexpr Triple triples[] = {
        { 100.0f, 0.7f, 6.0f },
        { 1000.0f, 1.0f, 12.0f },
        { 1000.0f, 1.0f, -12.0f },
        { 5000.0f, 3.0f, 3.0f },
        { 200.0f, 0.3f, -9.0f },
        { 8000.0f, 8.0f, 9.0f },
    };

    for (const auto& triple : triples)
    {
        INFO ("freq=" << triple.freqHz << " Hz, Q=" << triple.q << ", gain=" << triple.gainDb << " dB");

        const auto measuredDb = measureBandGainDb (2, false, triple.freqHz, triple.q, triple.gainDb, triple.freqHz);
        const auto referenceDb = static_cast<float> (
            TestHelpers::rbjPeakMagnitudeDb (testSampleRate, triple.freqHz, triple.q, triple.gainDb, triple.freqHz));

        CHECK (measuredDb == Catch::Approx (referenceDb).margin (0.5));
    }
}

TEST_CASE ("Band 1 in Bell mode matches the analytic RBJ peaking-EQ response within +-0.5 dB", "[dsp][static-response]")
{
    const auto measuredDb = measureBandGainDb (0, false, 300.0f, 2.0f, 8.0f, 300.0f);
    const auto referenceDb = static_cast<float> (TestHelpers::rbjPeakMagnitudeDb (testSampleRate, 300.0f, 2.0f, 8.0f, 300.0f));

    CHECK (measuredDb == Catch::Approx (referenceDb).margin (0.5));
}

TEST_CASE ("Band 1 LowShelf mode matches the analytic RBJ low-shelf response within +-0.5 dB", "[dsp][static-response]")
{
    // Probe deep in the shelf's low-frequency plateau, well below the
    // corner, where the response has fully settled to its plateau gain.
    static constexpr float cornerHz = 150.0f;
    static constexpr double probeHz = 30.0;

    for (const auto gainDb : { 9.0f, -9.0f })
    {
        INFO ("gain=" << gainDb << " dB");

        const auto measuredDb = measureBandGainDb (0, true, cornerHz, 1.0f /* ignored in shelf mode */, gainDb, probeHz);
        const auto referenceDb = static_cast<float> (
            TestHelpers::rbjShelfMagnitudeDb (testSampleRate, cornerHz, shelfQ, gainDb, probeHz, true));

        CHECK (measuredDb == Catch::Approx (referenceDb).margin (0.5));
    }
}

TEST_CASE ("Band 6 HighShelf mode matches the analytic RBJ high-shelf response within +-0.5 dB", "[dsp][static-response]")
{
    // Probe well above the corner, in the shelf's high-frequency plateau.
    static constexpr float cornerHz = 6000.0f;
    static constexpr double probeHz = 18000.0;

    for (const auto gainDb : { 9.0f, -9.0f })
    {
        INFO ("gain=" << gainDb << " dB");

        const auto measuredDb = measureBandGainDb (5, true, cornerHz, 1.0f /* ignored in shelf mode */, gainDb, probeHz);
        const auto referenceDb = static_cast<float> (
            TestHelpers::rbjShelfMagnitudeDb (testSampleRate, cornerHz, shelfQ, gainDb, probeHz, false));

        CHECK (measuredDb == Catch::Approx (referenceDb).margin (0.5));
    }
}

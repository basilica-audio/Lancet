#include "PluginProcessor.h"
#include "dsp/Detector.h"
#include "params/ParameterIds.h"
#include "TestHelpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <utility>

// v0.3.0 (docs/voicing-notes.md) "musically tuned ballistics per use case":
// each band's default Attack/Release now differs, tuned to that band's
// documented role along the existing frequency ladder - low-frequency
// boom/resonance control (Band 1) is meant to move slowly (avoid audible
// pumping on sustained low end); high-frequency sibilance/harshness control
// (Band 5) is meant to move fast (catch a transient before it's over). This
// file freezes that intent as a real, measured regression: it reads each
// band's *actual* shipped default Attack/Release straight out of a freshly
// constructed processor's APVTS (not a hand-duplicated constant), feeds
// those exact values into an isolated Detector, and measures a real
// step-response ordering - not just "the numbers differ", but "the
// resulting envelope-follower behaviour is ordered the way the documented
// use case requires."
namespace
{
    constexpr double testSampleRate = 48000.0;
    constexpr double toneHz = 1000.0;
    constexpr int settlePhaseSamples = 8192;
    constexpr float fullAmplitude = 0.8f;

    struct BandDefaultBallistics
    {
        float attackMs;
        float releaseMs;
    };

    juce::dsp::ProcessSpec makeSpec (int maxSamples)
    {
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = testSampleRate;
        spec.maximumBlockSize = static_cast<juce::uint32> (maxSamples);
        spec.numChannels = 1;
        return spec;
    }

    float readDefault (juce::AudioProcessorValueTreeState& apvts, const char* id)
    {
        auto* param = apvts.getParameter (id);
        REQUIRE (param != nullptr);
        return param->convertFrom0to1 (param->getDefaultValue());
    }

    // Reads Band N's actual shipped default Attack/Release straight from a
    // fresh processor's APVTS - see file comment for why this matters (a
    // regression on the real defaults, not a hand-duplicated copy of them).
    BandDefaultBallistics readBandDefaults (juce::AudioProcessorValueTreeState& apvts, const char* attackId, const char* releaseId)
    {
        return { readDefault (apvts, attackId), readDefault (apvts, releaseId) };
    }

    // Same technique as tests/AttackReleaseRangeTests.cpp's
    // measureAttackProgressDb(): step from silence to a full-level in-band
    // tone, with the given Attack (Release fixed deliberately slow/
    // irrelevant to this attack-phase measurement), and return the envelope
    // level (dBFS) `elapsedSamples` after the step.
    float measureAttackProgressDb (float attackMs, int elapsedSamples)
    {
        Detector detector;
        detector.prepare (makeSpec (juce::jmax (settlePhaseSamples, elapsedSamples)));
        detector.setFrequencyAndQ (static_cast<float> (toneHz), 1.0f);
        detector.setAttackMs (attackMs);
        detector.setReleaseMs (2000.0f);

        juce::AudioBuffer<float> silence (1, settlePhaseSamples);
        silence.clear();
        const juce::dsp::AudioBlock<const float> silentBlock (silence);
        detector.processSubBlock (silentBlock, 0, static_cast<size_t> (settlePhaseSamples));

        juce::AudioBuffer<float> stepBuffer (1, elapsedSamples);
        TestHelpers::fillWithSine (stepBuffer, testSampleRate, toneHz, fullAmplitude, settlePhaseSamples);
        const juce::dsp::AudioBlock<const float> stepBlock (stepBuffer);

        return detector.processSubBlock (stepBlock, 0, static_cast<size_t> (elapsedSamples));
    }

    // Mirrored for the release phase: settle at full level (fast, fixed
    // Attack, irrelevant to this measurement), then step down to silence,
    // and measure `elapsedSamples` later.
    float measureReleaseProgressDb (float releaseMs, int elapsedSamples)
    {
        Detector detector;
        detector.prepare (makeSpec (juce::jmax (settlePhaseSamples, elapsedSamples)));
        detector.setFrequencyAndQ (static_cast<float> (toneHz), 1.0f);
        detector.setAttackMs (0.1f);
        detector.setReleaseMs (releaseMs);

        juce::AudioBuffer<float> loud (1, settlePhaseSamples);
        TestHelpers::fillWithSine (loud, testSampleRate, toneHz, fullAmplitude);
        const juce::dsp::AudioBlock<const float> loudBlock (loud);
        detector.processSubBlock (loudBlock, 0, static_cast<size_t> (settlePhaseSamples));

        juce::AudioBuffer<float> silence (1, elapsedSamples);
        silence.clear();
        const juce::dsp::AudioBlock<const float> silentBlock (silence);

        return detector.processSubBlock (silentBlock, 0, static_cast<size_t> (elapsedSamples));
    }

    int msToSamples (double ms)
    {
        return juce::jmax (1, static_cast<int> ((ms / 1000.0) * testSampleRate));
    }
}

TEST_CASE ("Per-band default ballistics: shipped defaults match the documented voicing table "
           "(docs/voicing-notes.md)",
           "[dsp][voicing][ballistics-defaults]")
{
    LancetAudioProcessor processor;
    auto& apvts = processor.apvts;

    CHECK (readDefault (apvts, ParamIDs::b1Attack) == Catch::Approx (25.0f).margin (1.0e-3));
    CHECK (readDefault (apvts, ParamIDs::b1Release) == Catch::Approx (280.0f).margin (1.0e-3));

    CHECK (readDefault (apvts, ParamIDs::b2Attack) == Catch::Approx (15.0f).margin (1.0e-3));
    CHECK (readDefault (apvts, ParamIDs::b2Release) == Catch::Approx (180.0f).margin (1.0e-3));

    CHECK (readDefault (apvts, ParamIDs::b3Attack) == Catch::Approx (8.0f).margin (1.0e-3));
    CHECK (readDefault (apvts, ParamIDs::b3Release) == Catch::Approx (130.0f).margin (1.0e-3));

    CHECK (readDefault (apvts, ParamIDs::b4Attack) == Catch::Approx (4.0f).margin (1.0e-3));
    CHECK (readDefault (apvts, ParamIDs::b4Release) == Catch::Approx (100.0f).margin (1.0e-3));

    CHECK (readDefault (apvts, ParamIDs::b5Attack) == Catch::Approx (2.0f).margin (1.0e-3));
    CHECK (readDefault (apvts, ParamIDs::b5Release) == Catch::Approx (70.0f).margin (1.0e-3));

    CHECK (readDefault (apvts, ParamIDs::b6Attack) == Catch::Approx (3.0f).margin (1.0e-3));
    CHECK (readDefault (apvts, ParamIDs::b6Release) == Catch::Approx (90.0f).margin (1.0e-3));
}

TEST_CASE ("Per-band default Attack ballistics are measurably ordered slowest (Band 1, boom control) to "
           "fastest (Band 5, sibilance/harshness control)",
           "[dsp][voicing][ballistics-defaults]")
{
    LancetAudioProcessor processor;
    auto& apvts = processor.apvts;

    static constexpr std::pair<const char*, const char*> adjacentAttackPairs[] = {
        { ParamIDs::b1Attack, ParamIDs::b2Attack },
        { ParamIDs::b2Attack, ParamIDs::b3Attack },
        { ParamIDs::b3Attack, ParamIDs::b4Attack },
        { ParamIDs::b4Attack, ParamIDs::b5Attack },
    };

    for (const auto& pair : adjacentAttackPairs)
    {
        const auto slowerMs = readDefault (apvts, pair.first);
        const auto fasterMs = readDefault (apvts, pair.second);
        REQUIRE (fasterMs < slowerMs);

        // Geometric-mean elapsed time between the two - meaningfully
        // differentiates the pair regardless of the absolute ratio between
        // them (same idiom as tests/GainQCouplingTests.cpp's bisection).
        const auto elapsedMs = std::sqrt (static_cast<double> (slowerMs) * static_cast<double> (fasterMs));
        const auto elapsedSamples = msToSamples (elapsedMs);

        const auto slowerProgressDb = measureAttackProgressDb (slowerMs, elapsedSamples);
        const auto fasterProgressDb = measureAttackProgressDb (fasterMs, elapsedSamples);

        INFO ("slower attack=" << slowerMs << "ms -> " << slowerProgressDb << " dBFS; "
                                << "faster attack=" << fasterMs << "ms -> " << fasterProgressDb << " dBFS "
                                << "(elapsed=" << elapsedMs << " ms)");
        CHECK (std::isfinite (slowerProgressDb));
        CHECK (std::isfinite (fasterProgressDb));
        CHECK (fasterProgressDb > slowerProgressDb);
    }
}

TEST_CASE ("Per-band default Release ballistics are measurably ordered slowest (Band 1, boom control) to "
           "fastest (Band 5, sibilance/harshness control)",
           "[dsp][voicing][ballistics-defaults]")
{
    LancetAudioProcessor processor;
    auto& apvts = processor.apvts;

    static constexpr std::pair<const char*, const char*> adjacentReleasePairs[] = {
        { ParamIDs::b1Release, ParamIDs::b2Release },
        { ParamIDs::b2Release, ParamIDs::b3Release },
        { ParamIDs::b3Release, ParamIDs::b4Release },
        { ParamIDs::b4Release, ParamIDs::b5Release },
    };

    for (const auto& pair : adjacentReleasePairs)
    {
        const auto slowerMs = readDefault (apvts, pair.first);
        const auto fasterMs = readDefault (apvts, pair.second);
        REQUIRE (fasterMs < slowerMs);

        const auto elapsedMs = std::sqrt (static_cast<double> (slowerMs) * static_cast<double> (fasterMs));
        const auto elapsedSamples = msToSamples (elapsedMs);

        const auto slowerProgressDb = measureReleaseProgressDb (slowerMs, elapsedSamples);
        const auto fasterProgressDb = measureReleaseProgressDb (fasterMs, elapsedSamples);

        INFO ("slower release=" << slowerMs << "ms -> " << slowerProgressDb << " dBFS; "
                                 << "faster release=" << fasterMs << "ms -> " << fasterProgressDb << " dBFS "
                                 << "(elapsed=" << elapsedMs << " ms)");
        CHECK (std::isfinite (slowerProgressDb));
        CHECK (std::isfinite (fasterProgressDb));
        // Release decays TOWARD -100 dBFS floor - a faster release has
        // decayed further (more negative) than the slower one at the same
        // elapsed time.
        CHECK (fasterProgressDb < slowerProgressDb);
    }
}

TEST_CASE ("Band 6's default ballistics (air/fizz recovery shelf) sit close to Band 5, not the slow end",
           "[dsp][voicing][ballistics-defaults]")
{
    LancetAudioProcessor processor;
    auto& apvts = processor.apvts;

    const auto band2 = readBandDefaults (apvts, ParamIDs::b2Attack, ParamIDs::b2Release);
    const auto band6 = readBandDefaults (apvts, ParamIDs::b6Attack, ParamIDs::b6Release);

    // Band 6 is a fast recovery/air shelf, not a slow boom-control band -
    // its Attack/Release should sit well below Band 2's (mud/box region).
    CHECK (band6.attackMs < band2.attackMs);
    CHECK (band6.releaseMs < band2.releaseMs);
}

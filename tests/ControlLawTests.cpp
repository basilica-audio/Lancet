#include "PluginProcessor.h"
#include "params/ParameterIds.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

// Issue #4 control-law freeze (docs/voicing-notes.md, "Control-law
// analysis" section): the *shape* of every knob's value mapping - not just
// its endpoints and default - is part of the plugin's voicing, and changing
// it silently re-curves every host automation lane written against the
// parameter (the same concern docs/architecture.md's state-schema section
// raises for range changes). These tests freeze the mappings by property:
//
// - The log-mapped Freq/Attack/Release knobs put the design brief's own
//   sourced starting recipe (§1 item 5: "Range -6 dB, Attack 10 ms, Release
//   ~100 ms" at the documented problem frequencies) at mid-travel, where a
//   user's first reach lands.
// - The skewed Q knob centres the musically common 0.7 - 4 window.
// - Every shipped per-band Q/Threshold/Attack/Release default sits in the
//   middle half of its knob's travel - a default a knob can be nudged away
//   from in both directions with room to work, never pinned near an end
//   stop.
//
// Everything below is read from a freshly constructed processor's real
// parameter objects (convertFrom0to1/convertTo0to1), not re-derived from
// hand-copied range constants.
namespace
{
    juce::RangedAudioParameter* requireParam (juce::AudioProcessorValueTreeState& apvts, const char* id)
    {
        auto* param = apvts.getParameter (id);
        REQUIRE (param != nullptr);
        return param;
    }

    float valueAtTravel (juce::AudioProcessorValueTreeState& apvts, const char* id, float normalised)
    {
        return requireParam (apvts, id)->convertFrom0to1 (normalised);
    }

    float travelOfValue (juce::AudioProcessorValueTreeState& apvts, const char* id, float value)
    {
        return requireParam (apvts, id)->convertTo0to1 (value);
    }

    float travelOfDefault (juce::AudioProcessorValueTreeState& apvts, const char* id)
    {
        return requireParam (apvts, id)->getDefaultValue();
    }
}

TEST_CASE ("Control law: Freq's log mapping puts the 630 Hz default-on demo band at mid-travel",
           "[parameters][voicing][control-law]")
{
    LancetAudioProcessor processor;
    auto& apvts = processor.apvts;

    // True log mapping over 20 Hz - 20 kHz: mid-travel is the geometric mean
    // sqrt(20 * 20000) = 632.46 Hz - within 0.4% of Band 3's 630 Hz default,
    // so the band that ships enabled sits where the knob's travel is centred.
    const auto midTravelHz = valueAtTravel (apvts, ParamIDs::b3Freq, 0.5f);
    CHECK (midTravelHz == Catch::Approx (std::sqrt (20.0 * 20000.0)).epsilon (0.01));
    CHECK (travelOfDefault (apvts, ParamIDs::b3Freq) == Catch::Approx (0.5f).margin (0.005f));
}

TEST_CASE ("Control law: Attack/Release log mappings put the design brief's sourced starting recipe "
           "(10 ms / ~100 ms) at mid-travel",
           "[parameters][voicing][control-law]")
{
    LancetAudioProcessor processor;
    auto& apvts = processor.apvts;

    // Mid-travel of a log range is its geometric mean: sqrt(0.1 * 500)
    // = 7.07 ms for Attack, sqrt(5 * 1500) = 86.6 ms for Release.
    CHECK (valueAtTravel (apvts, ParamIDs::b3Attack, 0.5f) == Catch::Approx (std::sqrt (0.1 * 500.0)).epsilon (0.01));
    CHECK (valueAtTravel (apvts, ParamIDs::b3Release, 0.5f) == Catch::Approx (std::sqrt (5.0 * 1500.0)).epsilon (0.01));

    // The reference recipe the design brief quotes (§1 item 5: Attack 10 ms,
    // Release ~100 ms) lands within a few percent of half-travel - the
    // category's canonical starting values are where the knobs' resolution
    // is spent, not compressed against an end stop.
    const auto recipeAttackTravel = travelOfValue (apvts, ParamIDs::b3Attack, 10.0f);
    const auto recipeReleaseTravel = travelOfValue (apvts, ParamIDs::b3Release, 100.0f);

    INFO ("10 ms attack sits at " << recipeAttackTravel << " travel; 100 ms release at " << recipeReleaseTravel);
    CHECK (recipeAttackTravel > 0.45f);
    CHECK (recipeAttackTravel < 0.62f);
    CHECK (recipeReleaseTravel > 0.45f);
    CHECK (recipeReleaseTravel < 0.62f);
}

TEST_CASE ("Control law: Q's skew centres the musically common 0.7 - 4 window around mid-travel",
           "[parameters][voicing][control-law]")
{
    LancetAudioProcessor processor;
    auto& apvts = processor.apvts;

    // The 0.4 skew maps mid-travel to Q = 0.3 + 11.7 * 0.5^(1/0.4) = 2.37 -
    // inside the broad-to-surgical transition zone - with the wide/musical
    // 0.7 end and the narrow/surgical 4.0 end on opposite sides of centre.
    const auto midTravelQ = valueAtTravel (apvts, ParamIDs::b3Q, 0.5f);
    CHECK (midTravelQ == Catch::Approx (0.3 + 11.7 * std::pow (0.5, 1.0 / 0.4)).epsilon (0.01));

    CHECK (travelOfValue (apvts, ParamIDs::b3Q, 0.7f) < 0.5f);
    CHECK (travelOfValue (apvts, ParamIDs::b3Q, 4.0f) > 0.5f);
}

TEST_CASE ("Control law: every per-band Q/Threshold/Attack/Release default sits in the middle half of "
           "its knob's travel",
           "[parameters][voicing][control-law]")
{
    LancetAudioProcessor processor;
    auto& apvts = processor.apvts;

    static constexpr const char* voicedIds[] = {
        ParamIDs::b1Q, ParamIDs::b1Threshold, ParamIDs::b1Attack, ParamIDs::b1Release,
        ParamIDs::b2Q, ParamIDs::b2Threshold, ParamIDs::b2Attack, ParamIDs::b2Release,
        ParamIDs::b3Q, ParamIDs::b3Threshold, ParamIDs::b3Attack, ParamIDs::b3Release,
        ParamIDs::b4Q, ParamIDs::b4Threshold, ParamIDs::b4Attack, ParamIDs::b4Release,
        ParamIDs::b5Q, ParamIDs::b5Threshold, ParamIDs::b5Attack, ParamIDs::b5Release,
        ParamIDs::b6Q, ParamIDs::b6Threshold, ParamIDs::b6Attack, ParamIDs::b6Release,
    };

    for (const auto* id : voicedIds)
    {
        const auto travel = travelOfDefault (apvts, id);
        INFO (id << " default sits at " << travel << " of knob travel");
        CHECK (travel >= 0.25f);
        CHECK (travel <= 0.75f);
    }
}

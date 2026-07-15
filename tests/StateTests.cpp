#include "PluginProcessor.h"
#include "params/ParameterIds.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

// Guarantee #7 (docs/design-brief.md): "State round-trip: save -> reload ->
// identical parameter values."
TEST_CASE ("State round-trip preserves non-default values of every float/choice parameter", "[state]")
{
    LancetAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    // Bool parameters (On/Listen) are covered separately below - see that
    // test's comment for why (AudioParameterBool quantises to exactly
    // 0.0/1.0, so this test's "distinct fractional normalised value per
    // parameter" technique doesn't apply to them).
    static constexpr const char* floatAndChoiceIds[] = {
        ParamIDs::inTrim, ParamIDs::outTrim, ParamIDs::mix,

        ParamIDs::b1Type, ParamIDs::b1Freq, ParamIDs::b1Q, ParamIDs::b1Gain, ParamIDs::b1Range,
        ParamIDs::b1Threshold, ParamIDs::b1Attack, ParamIDs::b1Release,

        ParamIDs::b2Freq, ParamIDs::b2Q, ParamIDs::b2Gain, ParamIDs::b2Range,
        ParamIDs::b2Threshold, ParamIDs::b2Attack, ParamIDs::b2Release,

        ParamIDs::b3Freq, ParamIDs::b3Q, ParamIDs::b3Gain, ParamIDs::b3Range,
        ParamIDs::b3Threshold, ParamIDs::b3Attack, ParamIDs::b3Release,

        ParamIDs::b4Freq, ParamIDs::b4Q, ParamIDs::b4Gain, ParamIDs::b4Range,
        ParamIDs::b4Threshold, ParamIDs::b4Attack, ParamIDs::b4Release,

        ParamIDs::b5Freq, ParamIDs::b5Q, ParamIDs::b5Gain, ParamIDs::b5Range,
        ParamIDs::b5Threshold, ParamIDs::b5Attack, ParamIDs::b5Release,

        ParamIDs::b6Type, ParamIDs::b6Freq, ParamIDs::b6Q, ParamIDs::b6Gain, ParamIDs::b6Range,
        ParamIDs::b6Threshold, ParamIDs::b6Attack, ParamIDs::b6Release,
    };

    std::vector<juce::RangedAudioParameter*> params;
    std::vector<float> savedNormalisedValues;

    for (const auto* id : floatAndChoiceIds)
    {
        auto* param = processor.apvts.getParameter (id);
        REQUIRE (param != nullptr);
        params.push_back (param);
    }

    // Push every parameter to a distinct, non-default normalised value so
    // the round-trip assertion below can't pass by coincidence.
    for (size_t i = 0; i < params.size(); ++i)
    {
        auto normalisedValue = 0.2f + 0.6f * (static_cast<float> (i % 5) / 4.0f);

        if (std::abs (normalisedValue - params[i]->getDefaultValue()) < 0.05f)
            normalisedValue = std::fmod (normalisedValue + 0.37f, 1.0f);

        params[i]->setValueNotifyingHost (normalisedValue);
        savedNormalisedValues.push_back (params[i]->getValue());
    }

    juce::MemoryBlock savedState;
    processor.getStateInformation (savedState);
    REQUIRE (savedState.getSize() > 0);

    // Reset every parameter back to its default before restoring, so the
    // round-trip assertion below can't pass by accident.
    for (auto* param : params)
        param->setValueNotifyingHost (param->getDefaultValue());

    for (size_t i = 0; i < params.size(); ++i)
        REQUIRE (params[i]->getValue() != Catch::Approx (savedNormalisedValues[i]));

    processor.setStateInformation (savedState.getData(), static_cast<int> (savedState.getSize()));

    for (size_t i = 0; i < params.size(); ++i)
    {
        INFO ("parameter index = " << i);
        CHECK (params[i]->getValue() == Catch::Approx (savedNormalisedValues[i]).margin (1e-6));
    }
}

// Bool parameters (On/Listen, every band) round-trip separately from the
// float/choice sweep above - see that test's comment for why.
TEST_CASE ("State round-trip preserves every bool parameter (On/Listen, every band)", "[state]")
{
    LancetAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    static constexpr const char* boolIds[] = {
        ParamIDs::b1On, ParamIDs::b1Listen,
        ParamIDs::b2On, ParamIDs::b2Listen,
        ParamIDs::b3On, ParamIDs::b3Listen, // Band 3 defaults On - still round-trips like every other bool
        ParamIDs::b4On, ParamIDs::b4Listen,
        ParamIDs::b5On, ParamIDs::b5Listen,
        ParamIDs::b6On, ParamIDs::b6Listen,
    };

    std::vector<juce::RangedAudioParameter*> params;
    std::vector<bool> targetValues;

    for (const auto* id : boolIds)
    {
        auto* param = processor.apvts.getParameter (id);
        REQUIRE (param != nullptr);

        // Flip every one to the opposite of its default, so the round-trip
        // assertion below can't pass by coincidentally already sitting at
        // the post-restore value.
        const auto targetValue = param->getDefaultValue() < 0.5f;
        param->setValueNotifyingHost (targetValue ? 1.0f : 0.0f);

        params.push_back (param);
        targetValues.push_back (targetValue);
    }

    juce::MemoryBlock savedState;
    processor.getStateInformation (savedState);
    REQUIRE (savedState.getSize() > 0);

    for (auto* param : params)
        param->setValueNotifyingHost (param->getDefaultValue());

    for (size_t i = 0; i < params.size(); ++i)
        REQUIRE ((params[i]->getValue() > 0.5f) != targetValues[i]);

    processor.setStateInformation (savedState.getData(), static_cast<int> (savedState.getSize()));

    for (size_t i = 0; i < params.size(); ++i)
        CHECK ((params[i]->getValue() > 0.5f) == targetValues[i]);
}

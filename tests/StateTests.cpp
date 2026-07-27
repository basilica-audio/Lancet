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

    // Bool parameters (On/Listen/...) are covered separately below - see that
    // test's comment for why (AudioParameterBool quantises to exactly
    // 0.0/1.0, so this test's "distinct fractional normalised value per
    // parameter" technique doesn't apply to them). v0.4.0's two-valued
    // AudioParameterChoice pairs (SC Source, SC Mode) quantise the same way
    // and are covered there too, not here.
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

    // Two-valued AudioParameterChoice parameters (SC Source, SC Mode) are
    // listed here rather than with the float/choice set above: they snap to
    // exactly 0.0/1.0, so "flip it to the opposite of its default" is the only
    // technique that can distinguish a restored value from a default one.
    static constexpr const char* boolIds[] = {
        ParamIDs::b1On, ParamIDs::b1Listen, ParamIDs::b1AutoRelease, ParamIDs::b1GainQ, ParamIDs::b1Sat, ParamIDs::b1ScSource, ParamIDs::b1ScMode,
        ParamIDs::b2On, ParamIDs::b2Listen, ParamIDs::b2AutoRelease, ParamIDs::b2GainQ, ParamIDs::b2Sat, ParamIDs::b2ScSource, ParamIDs::b2ScMode,
        ParamIDs::b3On, ParamIDs::b3Listen, ParamIDs::b3AutoRelease, ParamIDs::b3GainQ, ParamIDs::b3Sat, ParamIDs::b3ScSource, ParamIDs::b3ScMode, // Band 3 defaults On - still round-trips like every other bool
        ParamIDs::b4On, ParamIDs::b4Listen, ParamIDs::b4AutoRelease, ParamIDs::b4GainQ, ParamIDs::b4Sat, ParamIDs::b4ScSource, ParamIDs::b4ScMode,
        ParamIDs::b5On, ParamIDs::b5Listen, ParamIDs::b5AutoRelease, ParamIDs::b5GainQ, ParamIDs::b5Sat, ParamIDs::b5ScSource, ParamIDs::b5ScMode,
        ParamIDs::b6On, ParamIDs::b6Listen, ParamIDs::b6AutoRelease, ParamIDs::b6GainQ, ParamIDs::b6Sat, ParamIDs::b6ScSource, ParamIDs::b6ScMode,
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

//==============================================================================
// v0.4.0 (SOTA brief F9/T9): the saved state gained a schema version.
//
// Nothing about schema 2 needs a value transform - every parameter added since
// schema 1 defaults to the behaviour that was there before it existed, so
// JUCE's own tolerant restore already produces the right result. The attribute
// is written now so that a *future* release which does need a transform (the
// planned Range/Q range widening changes host automation-curve mapping, which
// cannot be done silently) has a reliable way to tell what it is reading. That
// only works if the stamp is written and preserved from this release onward,
// which is what these cases pin.
TEST_CASE ("State schema: saved state carries stateVersion=2 on the root", "[state][schema]")
{
    LancetAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    juce::MemoryBlock savedState;
    processor.getStateInformation (savedState);

    const std::unique_ptr<juce::XmlElement> xml (
        processor.getXmlFromBinary (savedState.getData(), static_cast<int> (savedState.getSize())));

    REQUIRE (xml != nullptr);
    CHECK (xml->hasAttribute (LancetAudioProcessor::stateVersionAttribute));
    CHECK (xml->getIntAttribute (LancetAudioProcessor::stateVersionAttribute)
           == LancetAudioProcessor::currentStateVersion);
}

TEST_CASE ("State schema: a round trip preserves the version stamp", "[state][schema]")
{
    LancetAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    juce::MemoryBlock firstSave;
    processor.getStateInformation (firstSave);

    processor.setStateInformation (firstSave.getData(), static_cast<int> (firstSave.getSize()));
    CHECK (processor.getLoadedStateVersion() == LancetAudioProcessor::currentStateVersion);

    juce::MemoryBlock secondSave;
    processor.getStateInformation (secondSave);

    const std::unique_ptr<juce::XmlElement> xml (
        processor.getXmlFromBinary (secondSave.getData(), static_cast<int> (secondSave.getSize())));

    REQUIRE (xml != nullptr);
    CHECK (xml->getIntAttribute (LancetAudioProcessor::stateVersionAttribute)
           == LancetAudioProcessor::currentStateVersion);
}

TEST_CASE ("State schema: a state without the attribute is read as schema 1", "[state][schema]")
{
    // Everything Lancet ever wrote before v0.4.0 looks like this.
    const char* unversionedXml = R"(<PARAMETERS>
        <PARAM id="in_trim" value="1.5"/>
        <PARAM id="mix" value="70.0"/>
    </PARAMETERS>)";

    LancetAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    const std::unique_ptr<juce::XmlElement> xml (juce::XmlDocument::parse (unversionedXml));
    REQUIRE (xml != nullptr);

    juce::MemoryBlock binary;
    processor.copyXmlToBinary (*xml, binary);
    processor.setStateInformation (binary.getData(), static_cast<int> (binary.getSize()));

    CHECK (processor.getLoadedStateVersion() == 1);

    // ...and it still restores its values, because schema 1 and schema 2 share
    // the same tolerant import path.
    auto* inTrim = processor.apvts.getParameter (ParamIDs::inTrim);
    REQUIRE (inTrim != nullptr);
    CHECK (inTrim->convertFrom0to1 (inTrim->getValue()) == Catch::Approx (1.5f).margin (1.0e-3));
}

TEST_CASE ("State schema: re-saving a schema-1 state upgrades the stamp to 2", "[state][schema]")
{
    const char* unversionedXml = R"(<PARAMETERS>
        <PARAM id="in_trim" value="1.5"/>
    </PARAMETERS>)";

    LancetAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    const std::unique_ptr<juce::XmlElement> xml (juce::XmlDocument::parse (unversionedXml));
    REQUIRE (xml != nullptr);

    juce::MemoryBlock binary;
    processor.copyXmlToBinary (*xml, binary);
    processor.setStateInformation (binary.getData(), static_cast<int> (binary.getSize()));
    REQUIRE (processor.getLoadedStateVersion() == 1);

    juce::MemoryBlock resaved;
    processor.getStateInformation (resaved);

    const std::unique_ptr<juce::XmlElement> resavedXml (
        processor.getXmlFromBinary (resaved.getData(), static_cast<int> (resaved.getSize())));

    REQUIRE (resavedXml != nullptr);
    CHECK (resavedXml->getIntAttribute (LancetAudioProcessor::stateVersionAttribute) == 2);
}

#include "PluginProcessor.h"
#include "params/ParameterIds.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

// Guarantee #6 (docs/design-brief.md §4): "Tolerant state import test" - a
// serialized v0.1.0 AudioProcessorValueTreeState XML (all current v0.1.0
// bN_* IDs, none of the v2-new bN_autoRelease/bN_gainQ IDs, and none of
// v0.3.0's bN_sat either) loads into the current version without error, with
// every new per-band ID populated at its own default (off) and all
// pre-existing IDs' values preserved exactly, including Attack/Release
// values that now sit well inside the widened ranges (no clamping needed
// since the v1 range was a strict subset). v0.3.0 (docs/voicing-notes.md)
// only changes ParameterLayout *defaults* (Q/Threshold/Attack/Release now
// differ per band) and adds the new bN_sat boolean - it renames/removes no
// existing ID, so this guarantee's shape is unaffected.
//
// XML shape (JUCE 8.0.14, AudioProcessorValueTreeState.cpp - each
// parameter is a <PARAM id="..." value="..."/> child of the root
// "PARAMETERS" element, `value` always the plain/denormalised value): see
// AudioProcessorValueTreeState::flushToTree()/updateParameterConnectionsWithParamAdapter().
namespace
{
    // A hand-authored v0.1.0-shaped state: every v0.1.0 bN_* ID (no
    // bN_autoRelease/bN_gainQ at all - those parameter IDs didn't exist
    // yet), with a handful of bands pushed to distinctive non-default
    // values (including Attack/Release values that were already valid
    // under v0.1.0's narrower 0.5-100 ms/10-1000 ms range and remain
    // unchanged, unclamped values under v0.2.0's wider range) so the
    // "values preserved exactly" assertions below can't pass by
    // coincidence.
    const char* v010StateXml = R"(<PARAMETERS>
        <PARAM id="in_trim" value="3.5"/>
        <PARAM id="out_trim" value="-2.0"/>
        <PARAM id="mix" value="80.0"/>

        <PARAM id="b1_on" value="1.0"/>
        <PARAM id="b1_type" value="1.0"/>
        <PARAM id="b1_freq" value="180.0"/>
        <PARAM id="b1_q" value="0.8"/>
        <PARAM id="b1_gain" value="-4.5"/>
        <PARAM id="b1_range" value="-6.0"/>
        <PARAM id="b1_thresh" value="-22.0"/>
        <PARAM id="b1_attack" value="50.0"/>
        <PARAM id="b1_release" value="300.0"/>
        <PARAM id="b1_listen" value="0.0"/>

        <PARAM id="b2_on" value="0.0"/>
        <PARAM id="b2_freq" value="250.0"/>
        <PARAM id="b2_q" value="1.0"/>
        <PARAM id="b2_gain" value="0.0"/>
        <PARAM id="b2_range" value="0.0"/>
        <PARAM id="b2_thresh" value="-30.0"/>
        <PARAM id="b2_attack" value="5.0"/>
        <PARAM id="b2_release" value="150.0"/>
        <PARAM id="b2_listen" value="0.0"/>

        <PARAM id="b3_on" value="1.0"/>
        <PARAM id="b3_freq" value="2500.0"/>
        <PARAM id="b3_q" value="3.2"/>
        <PARAM id="b3_gain" value="0.0"/>
        <PARAM id="b3_range" value="-9.0"/>
        <PARAM id="b3_thresh" value="-18.0"/>
        <PARAM id="b3_attack" value="0.5"/>
        <PARAM id="b3_release" value="1000.0"/>
        <PARAM id="b3_listen" value="0.0"/>

        <PARAM id="b4_on" value="0.0"/>
        <PARAM id="b4_freq" value="1600.0"/>
        <PARAM id="b4_q" value="1.0"/>
        <PARAM id="b4_gain" value="0.0"/>
        <PARAM id="b4_range" value="0.0"/>
        <PARAM id="b4_thresh" value="-30.0"/>
        <PARAM id="b4_attack" value="5.0"/>
        <PARAM id="b4_release" value="150.0"/>
        <PARAM id="b4_listen" value="0.0"/>

        <PARAM id="b5_on" value="0.0"/>
        <PARAM id="b5_freq" value="4000.0"/>
        <PARAM id="b5_q" value="1.0"/>
        <PARAM id="b5_gain" value="0.0"/>
        <PARAM id="b5_range" value="0.0"/>
        <PARAM id="b5_thresh" value="-30.0"/>
        <PARAM id="b5_attack" value="5.0"/>
        <PARAM id="b5_release" value="150.0"/>
        <PARAM id="b5_listen" value="0.0"/>

        <PARAM id="b6_on" value="1.0"/>
        <PARAM id="b6_type" value="1.0"/>
        <PARAM id="b6_freq" value="8500.0"/>
        <PARAM id="b6_q" value="1.0"/>
        <PARAM id="b6_gain" value="1.5"/>
        <PARAM id="b6_range" value="0.0"/>
        <PARAM id="b6_thresh" value="-30.0"/>
        <PARAM id="b6_attack" value="0.5"/>
        <PARAM id="b6_release" value="10.0"/>
        <PARAM id="b6_listen" value="0.0"/>
    </PARAMETERS>)";

    float getParam (LancetAudioProcessor& processor, const char* id)
    {
        auto* param = processor.apvts.getParameter (id);
        REQUIRE (param != nullptr);
        return param->convertFrom0to1 (param->getValue());
    }
}

TEST_CASE ("Tolerant import: a v0.1.0-shaped state (no bN_autoRelease/bN_gainQ) loads without error", "[state][tolerant-import]")
{
    LancetAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    const std::unique_ptr<juce::XmlElement> xml (juce::XmlDocument::parse (v010StateXml));
    REQUIRE (xml != nullptr);

    juce::MemoryBlock binary;
    processor.copyXmlToBinary (*xml, binary);

    CHECK_NOTHROW (processor.setStateInformation (binary.getData(), static_cast<int> (binary.getSize())));
}

TEST_CASE ("Tolerant import: every pre-existing v0.1.0 parameter value is preserved exactly", "[state][tolerant-import]")
{
    LancetAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    const std::unique_ptr<juce::XmlElement> xml (juce::XmlDocument::parse (v010StateXml));
    REQUIRE (xml != nullptr);
    juce::MemoryBlock binary;
    processor.copyXmlToBinary (*xml, binary);
    processor.setStateInformation (binary.getData(), static_cast<int> (binary.getSize()));

    CHECK (getParam (processor, ParamIDs::inTrim) == Catch::Approx (3.5f).margin (1.0e-3));
    CHECK (getParam (processor, ParamIDs::outTrim) == Catch::Approx (-2.0f).margin (1.0e-3));
    CHECK (getParam (processor, ParamIDs::mix) == Catch::Approx (80.0f).margin (1.0e-3));

    CHECK (getParam (processor, ParamIDs::b1On) == Catch::Approx (1.0f));
    CHECK (getParam (processor, ParamIDs::b1Type) == Catch::Approx (1.0f));
    CHECK (getParam (processor, ParamIDs::b1Freq) == Catch::Approx (180.0f).margin (1.0e-2));
    CHECK (getParam (processor, ParamIDs::b1Q) == Catch::Approx (0.8f).margin (1.0e-3));
    CHECK (getParam (processor, ParamIDs::b1Gain) == Catch::Approx (-4.5f).margin (1.0e-3));
    CHECK (getParam (processor, ParamIDs::b1Range) == Catch::Approx (-6.0f).margin (1.0e-3));
    CHECK (getParam (processor, ParamIDs::b1Threshold) == Catch::Approx (-22.0f).margin (1.0e-3));

    // Attack/Release values that were already valid under v0.1.0's
    // narrower range and remain unchanged (no clamping) under v0.2.0's
    // wider one - the specific proof this guarantee calls out.
    CHECK (getParam (processor, ParamIDs::b1Attack) == Catch::Approx (50.0f).margin (1.0e-2));
    CHECK (getParam (processor, ParamIDs::b1Release) == Catch::Approx (300.0f).margin (1.0e-2));
    CHECK (getParam (processor, ParamIDs::b3Attack) == Catch::Approx (0.5f).margin (1.0e-3)); // v0.1.0's old Attack floor
    CHECK (getParam (processor, ParamIDs::b3Release) == Catch::Approx (1000.0f).margin (1.0e-2)); // v0.1.0's old Release ceiling
    CHECK (getParam (processor, ParamIDs::b6Attack) == Catch::Approx (0.5f).margin (1.0e-3));
    CHECK (getParam (processor, ParamIDs::b6Release) == Catch::Approx (10.0f).margin (1.0e-2)); // v0.1.0's old Release floor

    CHECK (getParam (processor, ParamIDs::b3On) == Catch::Approx (1.0f));
    CHECK (getParam (processor, ParamIDs::b3Freq) == Catch::Approx (2500.0f).margin (1.0e-1));
    CHECK (getParam (processor, ParamIDs::b3Q) == Catch::Approx (3.2f).margin (1.0e-3));
    CHECK (getParam (processor, ParamIDs::b3Range) == Catch::Approx (-9.0f).margin (1.0e-3));
    CHECK (getParam (processor, ParamIDs::b3Threshold) == Catch::Approx (-18.0f).margin (1.0e-3));

    CHECK (getParam (processor, ParamIDs::b6On) == Catch::Approx (1.0f));
    CHECK (getParam (processor, ParamIDs::b6Type) == Catch::Approx (1.0f));
    CHECK (getParam (processor, ParamIDs::b6Freq) == Catch::Approx (8500.0f).margin (1.0f));
    CHECK (getParam (processor, ParamIDs::b6Gain) == Catch::Approx (1.5f).margin (1.0e-3));
}

TEST_CASE ("Tolerant import: the new v0.2.0/v0.3.0 per-band IDs (bN_autoRelease/bN_gainQ/bN_sat) populate at their "
           "default (off) on a freshly-constructed instance loading an old session",
           "[state][tolerant-import]")
{
    // Matches the realistic host scenario this guarantee is about: a
    // freshly instantiated v0.3.0 plugin loading a v0.1.0 session file for
    // the first time - JUCE's AudioProcessorValueTreeState::replaceState()
    // leaves any parameter ID absent from the loaded tree at whatever value
    // it already had (see updateParameterConnectionsToChildTrees() -
    // there's no automatic "reset to ParameterLayout default" step for
    // unmatched IDs), which is why this must be a *fresh* processor: its
    // construction-time value for these new IDs already IS the declared
    // default (false), and nothing has touched it since. bN_sat (v0.3.0,
    // docs/voicing-notes.md) is absent from v010StateXml exactly like
    // bN_autoRelease/bN_gainQ were, so it exercises the same code path.
    LancetAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    static constexpr const char* newBoolIds[] = {
        ParamIDs::b1AutoRelease, ParamIDs::b1GainQ, ParamIDs::b1Sat,
        ParamIDs::b2AutoRelease, ParamIDs::b2GainQ, ParamIDs::b2Sat,
        ParamIDs::b3AutoRelease, ParamIDs::b3GainQ, ParamIDs::b3Sat,
        ParamIDs::b4AutoRelease, ParamIDs::b4GainQ, ParamIDs::b4Sat,
        ParamIDs::b5AutoRelease, ParamIDs::b5GainQ, ParamIDs::b5Sat,
        ParamIDs::b6AutoRelease, ParamIDs::b6GainQ, ParamIDs::b6Sat,
    };

    const std::unique_ptr<juce::XmlElement> xml (juce::XmlDocument::parse (v010StateXml));
    REQUIRE (xml != nullptr);
    juce::MemoryBlock binary;
    processor.copyXmlToBinary (*xml, binary);
    processor.setStateInformation (binary.getData(), static_cast<int> (binary.getSize()));

    for (const auto* id : newBoolIds)
    {
        CAPTURE (id);
        auto* param = dynamic_cast<juce::AudioParameterBool*> (processor.apvts.getParameter (id));
        REQUIRE (param != nullptr);
        CHECK (param->get() == false);
    }
}

TEST_CASE ("Tolerant import control: a state that DOES include bN_autoRelease/bN_gainQ/bN_sat is read, not ignored",
           "[state][tolerant-import]")
{
    // Complements the "absent -> default" test above: proves the importer
    // genuinely reads these IDs when present rather than unconditionally
    // leaving them false regardless of the loaded state's content.
    LancetAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    constexpr const char* v030StateWithNewIdsXml = R"(<PARAMETERS>
        <PARAM id="b1_autoRelease" value="1.0"/>
        <PARAM id="b1_gainQ" value="1.0"/>
        <PARAM id="b1_sat" value="1.0"/>
    </PARAMETERS>)";

    const std::unique_ptr<juce::XmlElement> xml (juce::XmlDocument::parse (v030StateWithNewIdsXml));
    REQUIRE (xml != nullptr);
    juce::MemoryBlock binary;
    processor.copyXmlToBinary (*xml, binary);
    processor.setStateInformation (binary.getData(), static_cast<int> (binary.getSize()));

    auto* autoRelease = dynamic_cast<juce::AudioParameterBool*> (processor.apvts.getParameter (ParamIDs::b1AutoRelease));
    auto* gainQ = dynamic_cast<juce::AudioParameterBool*> (processor.apvts.getParameter (ParamIDs::b1GainQ));
    auto* sat = dynamic_cast<juce::AudioParameterBool*> (processor.apvts.getParameter (ParamIDs::b1Sat));
    REQUIRE (autoRelease != nullptr);
    REQUIRE (gainQ != nullptr);
    REQUIRE (sat != nullptr);

    CHECK (autoRelease->get() == true);
    CHECK (gainQ->get() == true);
    CHECK (sat->get() == true);
}

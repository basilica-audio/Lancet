#include "PluginProcessor.h"
#include "params/ParameterIds.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

// Regression test for an assumption tests/TolerantImportTests.cpp's
// hand-authored v0.1.0 XML fixture depends on: JUCE 8.0.14's
// AudioProcessorValueTreeState exports its state as a root element (named
// after the string passed to the APVTS constructor - "PARAMETERS" here)
// whose children are <PARAM id="..." value="..."/> elements, one per
// parameter, `value` always the plain/denormalised value (confirmed by
// reading juce_AudioProcessorValueTreeState.cpp's
// updateParameterConnectionsToChildTrees()/flushToTree(), which use a
// `valueType` of "PARAM" and property IDs "id"/"value"). If a future JUCE
// upgrade ever changes this shape, this test - not just
// TolerantImportTests.cpp's downstream symptoms - should be the first
// thing to fail.
TEST_CASE ("State XML shape: getStateInformation() exports <PARAM id=\"...\" value=\"...\"/> children on a "
           "\"PARAMETERS\" root",
           "[state]")
{
    LancetAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    auto* inTrimParam = processor.apvts.getParameter (ParamIDs::inTrim);
    REQUIRE (inTrimParam != nullptr);
    inTrimParam->setValueNotifyingHost (inTrimParam->convertTo0to1 (5.5f));

    juce::MemoryBlock binary;
    processor.getStateInformation (binary);

    const std::unique_ptr<juce::XmlElement> xml (processor.getXmlFromBinary (binary.getData(), static_cast<int> (binary.getSize())));
    REQUIRE (xml != nullptr);
    CHECK (xml->getTagName() == juce::String ("PARAMETERS"));

    auto* inTrimElement = xml->getChildByAttribute ("id", ParamIDs::inTrim);
    REQUIRE (inTrimElement != nullptr);
    CHECK (inTrimElement->getTagName() == juce::String ("PARAM"));
    CHECK (inTrimElement->getDoubleAttribute ("value") == Catch::Approx (5.5).margin (1.0e-3));
}

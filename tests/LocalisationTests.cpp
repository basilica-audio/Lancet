#include <BinaryData.h>

#include <juce_core/juce_core.h>

#include <catch2/catch_test_macros.hpp>

// M2 i18n frame coverage (.scaffold/specs/preset-system-m2.md's "I18N"
// section): "the de mapping parses; every TRANS key present in de.txt
// (script or test iterates keys); parameter names verifiably NOT in the
// mapping." resources/i18n/de.txt and src/presets/PresetBar.cpp/
// PresetManager.cpp are copied verbatim from the basilica-audio/nave pilot
// (their TRANS() key set is therefore unchanged); this test file is this
// repo's own coverage of the binding spec requirement.
namespace
{
    constexpr const char* frameKeys[] = {
        "Init",
        "Factory",
        "User",
        "Set current as default",
        "Save",
        "Save As...",
        "Delete",
        "Import...",
        "Export...",
        "Enter a name for the new preset:",
        "Preset name",
        "Cancel",
        "Import a preset or preset bank...",
        "Import failed",
        "Export preset...",
        "This file is not a valid preset.",
        "This preset was saved by an incompatible version of the preset format.",
        "This preset file belongs to a different plugin.",
    };

    // Core/DSP terminology (parameter names, units) that must NEVER be
    // translated anywhere in this plugin - deliberately absent from
    // resources/i18n/de.txt. Mirrors ParameterLayout.cpp's label prefixes
    // and PluginEditor.cpp's own hardcoded knob/toggle labels.
    constexpr const char* parameterNames[] = {
        "Input Trim", "Output Trim", "Mix",
        "Band 1", "Band 2", "Band 3", "Band 4", "Band 5", "Band 6",
        "On", "Freq", "Q", "Gain", "Range", "Threshold", "Thresh", "Attack", "Release",
        "Auto Release", "Gain/Q", "Listen",
        "Bell", "Low Shelf", "High Shelf",
        "Hz", "dB", "ms", "%",
    };
}

TEST_CASE ("Localisation: resources/i18n/de.txt parses as a valid JUCE LocalisedStrings mapping", "[i18n]")
{
    REQUIRE (BinaryData::de_txt != nullptr);
    REQUIRE (BinaryData::de_txtSize > 0);

    const auto text = juce::String::fromUTF8 (BinaryData::de_txt, BinaryData::de_txtSize);
    CHECK_NOTHROW (juce::LocalisedStrings (text, true));

    const juce::LocalisedStrings mappings (text, true);
    CHECK (mappings.getLanguageName() == juce::String ("German"));
}

TEST_CASE ("Localisation: every PresetBar/PresetManager TRANS() key has a German translation", "[i18n]")
{
    const auto text = juce::String::fromUTF8 (BinaryData::de_txt, BinaryData::de_txtSize);
    const juce::LocalisedStrings mappings (text, true);
    const auto& keyValueMap = mappings.getMappings();

    for (const auto* key : frameKeys)
    {
        CAPTURE (key);

        // Checked via the raw key/value map rather than translate()'s
        // return value: "Init" is a deliberate identity mapping ("Init" =
        // "Init" in resources/i18n/de.txt - the category name is kept
        // as-is in German too), so a "translated value differs from the
        // English key" heuristic would misreport it as absent.
        CHECK (keyValueMap.containsKey (juce::String (key)));
    }
}

TEST_CASE ("Localisation: parameter names and units are NOT present in the German mapping", "[i18n]")
{
    const auto text = juce::String::fromUTF8 (BinaryData::de_txt, BinaryData::de_txtSize);
    const juce::LocalisedStrings mappings (text, true);
    const auto& keyValueMap = mappings.getMappings();

    for (const auto* name : parameterNames)
    {
        CAPTURE (name);
        CHECK_FALSE (keyValueMap.containsKey (juce::String (name)));
    }
}

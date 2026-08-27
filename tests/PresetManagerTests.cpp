#include "PluginProcessor.h"
#include "params/ParameterIds.h"
#include "presets/PresetManager.h"

#include <BinaryData.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <vector>

// M2 preset system tests (.scaffold/specs/preset-system-m2.md's "Tests"
// section - each TEST_CASE below maps to one of that section's numbered
// items, called out in the test names/comments). Adapted from
// basilica-audio/nave's pilot implementation (tests/PresetManagerTests.cpp)
// - see docs/preset-system-notes.md there for the replication recipe this
// follows.
namespace
{
    using basilica::presets::FactoryPresetAsset;
    using basilica::presets::PresetManager;
    using basilica::presets::PresetManagerConfig;

    // Mirrors PluginProcessor.cpp's own makeFactoryPresetAssets() - kept as
    // an independent copy here so this test file can construct its own,
    // fully isolated PresetManager instances (see makeIsolatedConfig()
    // below) without depending on production wiring internals.
    std::vector<FactoryPresetAsset> makeTestFactoryPresetAssets()
    {
        return {
            { BinaryData::default_json, BinaryData::default_jsonSize },
            { BinaryData::gentleGlue_json, BinaryData::gentleGlue_jsonSize },
            { BinaryData::deEssStack_json, BinaryData::deEssStack_jsonSize },
            { BinaryData::transientSnareCrack_json, BinaryData::transientSnareCrack_jsonSize },
            { BinaryData::mixBussSettle_json, BinaryData::mixBussSettle_jsonSize },
            { BinaryData::slowTonalRide_json, BinaryData::slowTonalRide_jsonSize },
            { BinaryData::chestResonanceTamer_json, BinaryData::chestResonanceTamer_jsonSize },
            { BinaryData::fastRecoveryDemo_json, BinaryData::fastRecoveryDemo_jsonSize },
            { BinaryData::listenCheck_json, BinaryData::listenCheck_jsonSize },
            { BinaryData::analogWarmthLift_json, BinaryData::analogWarmthLift_jsonSize },
            { BinaryData::sidechainCarve_json, BinaryData::sidechainCarve_jsonSize },
        };
    }

    // A fresh, isolated scratch directory per test case, so this file never
    // reads or writes the real ~/Library/Audio/Presets/... (or Windows
    // equivalent) location on the machine running the tests - see
    // PresetManagerConfig::userPresetsDirectoryOverrideForTests. Deleted on
    // destruction.
    struct ScopedTestDirectory
    {
        ScopedTestDirectory()
            : dir (juce::File::getSpecialLocation (juce::File::tempDirectory)
                       .getChildFile ("LancetPresetManagerTests")
                       .getChildFile (juce::String (juce::Time::getHighResolutionTicks())
                                       + "_" + juce::String (juce::Random::getSystemRandom().nextInt (1000000))))
        {
            dir.createDirectory();
        }

        ~ScopedTestDirectory()
        {
            dir.deleteRecursively();
        }

        JUCE_DECLARE_NON_COPYABLE (ScopedTestDirectory)

        juce::File dir;
    };

    PresetManagerConfig makeIsolatedConfig (const juce::File& userDir)
    {
        PresetManagerConfig config;
        config.pluginId = "com.yvesvogl.lancet";
        config.pluginName = "Lancet";
        config.manufacturerName = "Basilica Audio";
        config.pluginVersion = "0.2.0-test";
        config.userPresetsDirectoryOverrideForTests = userDir;
        return config;
    }

    void setParam (LancetAudioProcessor& processor, const char* id, float realValue)
    {
        auto* param = processor.apvts.getParameter (id);
        REQUIRE (param != nullptr);
        param->setValueNotifyingHost (param->convertTo0to1 (realValue));
    }

    float getParam (LancetAudioProcessor& processor, const char* id)
    {
        auto* param = processor.apvts.getParameter (id);
        REQUIRE (param != nullptr);
        return param->convertFrom0to1 (param->getValue());
    }
}

//==============================================================================
// 1. Save -> load round-trip restores every parameter exactly.
TEST_CASE ("PresetManager: save -> load round-trip restores every parameter exactly", "[presets]")
{
    LancetAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    ScopedTestDirectory scratch;
    PresetManager manager (processor.apvts, makeIsolatedConfig (scratch.dir), makeTestFactoryPresetAssets());

    setParam (processor, ParamIDs::inTrim, 4.0f);
    setParam (processor, ParamIDs::mix, 60.0f);
    setParam (processor, ParamIDs::b2Freq, 300.0f);
    setParam (processor, ParamIDs::b2Range, -5.0f);
    setParam (processor, ParamIDs::b2Attack, 12.0f);
    setParam (processor, ParamIDs::b2Release, 220.0f);

    REQUIRE (manager.saveUserPreset ("Round Trip", "Init"));

    // Perturb every parameter away from the saved values before reloading,
    // so the assertions below can't pass by accident.
    setParam (processor, ParamIDs::inTrim, -8.0f);
    setParam (processor, ParamIDs::mix, 10.0f);
    setParam (processor, ParamIDs::b2Freq, 8000.0f);
    setParam (processor, ParamIDs::b2Range, 10.0f);
    setParam (processor, ParamIDs::b2Attack, 400.0f);
    setParam (processor, ParamIDs::b2Release, 900.0f);

    REQUIRE (manager.loadPreset ("Round Trip"));

    CHECK (getParam (processor, ParamIDs::inTrim) == Catch::Approx (4.0f).margin (1.0e-3));
    CHECK (getParam (processor, ParamIDs::mix) == Catch::Approx (60.0f).margin (1.0e-3));
    CHECK (getParam (processor, ParamIDs::b2Freq) == Catch::Approx (300.0f).margin (1.0e-1));
    CHECK (getParam (processor, ParamIDs::b2Range) == Catch::Approx (-5.0f).margin (1.0e-3));
    CHECK (getParam (processor, ParamIDs::b2Attack) == Catch::Approx (12.0f).margin (1.0e-2));
    CHECK (getParam (processor, ParamIDs::b2Release) == Catch::Approx (220.0f).margin (1.0e-2));
}

//==============================================================================
// 2. Import ignores unknown IDs, keeps defaults for missing IDs.
TEST_CASE ("PresetManager: import ignores unknown parameter IDs and keeps defaults for missing ones", "[presets]")
{
    LancetAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    ScopedTestDirectory scratch;
    PresetManager manager (processor.apvts, makeIsolatedConfig (scratch.dir), makeTestFactoryPresetAssets());

    // Move mix and b4_freq away from their defaults so it's meaningful when
    // the import below leaves them untouched (they're absent from
    // "parameters").
    setParam (processor, ParamIDs::mix, 55.0f);
    setParam (processor, ParamIDs::b4Freq, 9000.0f);

    // A fixture JSON generated inline (not committed under tests/fixtures/)
    // to avoid brittle relative-path resolution across CI runners with
    // different working directories - this is the forward/backward-compat
    // scenario the spec's "fixture JSONs in tests/" line calls for: an
    // unknown ID ("futureParameter", simulating a newer plugin version's
    // preset) and two known IDs (b1_freq/b1_range), deliberately omitting
    // mix/b4_freq.
    const juce::String fixtureJson = R"({
        "format": "basilica-preset-1",
        "plugin": "com.yvesvogl.lancet",
        "pluginVersion": "9.9.9",
        "name": "Forward Compat Fixture",
        "category": "Init",
        "parameters": { "b1_freq": 220.0, "b1_range": -7.0, "futureParameter": 42.0 }
    })";

    const auto fixtureFile = juce::File::createTempFile (".basilicapreset");
    REQUIRE (fixtureFile.replaceWithText (fixtureJson));

    juce::String errorMessage;
    REQUIRE (manager.importPresetFile (fixtureFile, errorMessage));
    CHECK (errorMessage.isEmpty());

    // Known IDs present in the fixture were applied...
    CHECK (getParam (processor, ParamIDs::b1Freq) == Catch::Approx (220.0f).margin (1.0e-1));
    CHECK (getParam (processor, ParamIDs::b1Range) == Catch::Approx (-7.0f).margin (1.0e-3));

    // ...IDs absent from the fixture were reset to their ParameterLayout
    // defaults (loadPreset()/importPresetFile() always reset-then-apply -
    // see PresetManager.h), not left at the pre-import 55%/9000 Hz values.
    auto* mixParam = processor.apvts.getParameter (ParamIDs::mix);
    auto* b4FreqParam = processor.apvts.getParameter (ParamIDs::b4Freq);
    CHECK (getParam (processor, ParamIDs::mix) == Catch::Approx (mixParam->convertFrom0to1 (mixParam->getDefaultValue())).margin (1.0e-3));
    CHECK (getParam (processor, ParamIDs::b4Freq) == Catch::Approx (b4FreqParam->convertFrom0to1 (b4FreqParam->getDefaultValue())).margin (1.0e-1));

    fixtureFile.deleteFile();
}

//==============================================================================
// 3. Import refuses wrong-plugin and wrong-format files.
TEST_CASE ("PresetManager: import refuses a preset belonging to a different plugin", "[presets]")
{
    LancetAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    ScopedTestDirectory scratch;
    PresetManager manager (processor.apvts, makeIsolatedConfig (scratch.dir), makeTestFactoryPresetAssets());

    const juce::String wrongPluginJson = R"({
        "format": "basilica-preset-1",
        "plugin": "com.yvesvogl.overture",
        "pluginVersion": "0.2.0",
        "name": "Not Lancet's",
        "category": "Init",
        "parameters": { "b1_freq": 999.0 }
    })";

    const auto file = juce::File::createTempFile (".basilicapreset");
    REQUIRE (file.replaceWithText (wrongPluginJson));

    juce::String errorMessage;
    CHECK_FALSE (manager.importPresetFile (file, errorMessage));
    CHECK (errorMessage.isNotEmpty());

    // State must be left untouched - b1_freq must NOT have picked up 999.
    CHECK (getParam (processor, ParamIDs::b1Freq) != Catch::Approx (999.0f));

    file.deleteFile();
}

TEST_CASE ("PresetManager: import refuses a file with an incompatible format tag", "[presets]")
{
    LancetAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    ScopedTestDirectory scratch;
    PresetManager manager (processor.apvts, makeIsolatedConfig (scratch.dir), makeTestFactoryPresetAssets());

    const juce::String wrongFormatJson = R"({
        "format": "some-other-format-2",
        "plugin": "com.yvesvogl.lancet",
        "pluginVersion": "0.2.0",
        "name": "Wrong Format",
        "category": "Init",
        "parameters": { "b1_freq": 999.0 }
    })";

    const auto file = juce::File::createTempFile (".basilicapreset");
    REQUIRE (file.replaceWithText (wrongFormatJson));

    juce::String errorMessage;
    CHECK_FALSE (manager.importPresetFile (file, errorMessage));
    CHECK (errorMessage.isNotEmpty());

    file.deleteFile();
}

//==============================================================================
// 4. Factory presets all parse and load.
TEST_CASE ("PresetManager: every factory preset parses and loads without error", "[presets]")
{
    LancetAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    ScopedTestDirectory scratch;
    PresetManager manager (processor.apvts, makeIsolatedConfig (scratch.dir), makeTestFactoryPresetAssets());

    const auto all = manager.getAllPresets();
    const auto factoryCount = std::count_if (all.begin(), all.end(), [] (auto& e) { return e.isFactory; });

    REQUIRE (factoryCount == 11); // docs/presets.md's Factory Presets table

    for (auto& entry : all)
    {
        if (! entry.isFactory)
            continue;

        CAPTURE (entry.name);
        CHECK (manager.loadPreset (entry.name));
        CHECK (manager.isCurrentPresetFactory());
        CHECK (manager.getCurrentPresetName() == entry.name);
    }
}

TEST_CASE ("PresetManager: factory preset content is plausible (Default is Init category, all parameters in range)", "[presets]")
{
    LancetAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    ScopedTestDirectory scratch;
    PresetManager manager (processor.apvts, makeIsolatedConfig (scratch.dir), makeTestFactoryPresetAssets());

    const auto all = manager.getAllPresets();
    const auto defaultEntry = std::find_if (all.begin(), all.end(), [] (auto& e) { return e.name == "Default"; });

    REQUIRE (defaultEntry != all.end());
    CHECK (defaultEntry->category == "Init");
    CHECK (defaultEntry->isFactory);

    // Loading every factory preset must leave every parameter's live value
    // inside its own ParameterLayout range - APVTS's setValueNotifyingHost()
    // clamps out-of-range normalised input, so an out-of-range preset value
    // wouldn't crash, but it would silently mean the JSON doesn't say what
    // the plugin actually does - worth catching explicitly.
    for (auto& entry : all)
    {
        if (! entry.isFactory)
            continue;

        REQUIRE (manager.loadPreset (entry.name));

        CHECK (getParam (processor, ParamIDs::b1Freq) >= 20.0f);
        CHECK (getParam (processor, ParamIDs::b1Freq) <= 20000.0f);
        CHECK (getParam (processor, ParamIDs::mix) >= 0.0f);
        CHECK (getParam (processor, ParamIDs::mix) <= 100.0f);
        CHECK (getParam (processor, ParamIDs::b3Attack) >= 0.1f);
        CHECK (getParam (processor, ParamIDs::b3Attack) <= 500.0f);
        CHECK (getParam (processor, ParamIDs::b3Release) >= 5.0f);
        CHECK (getParam (processor, ParamIDs::b3Release) <= 1500.0f);
    }
}

//==============================================================================
// 5. Default resolution order (user Default > factory Default > plain defaults).
TEST_CASE ("PresetManager: applyStartupDefault() loads the factory Default when no user Default exists", "[presets]")
{
    LancetAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    ScopedTestDirectory scratch;
    PresetManager manager (processor.apvts, makeIsolatedConfig (scratch.dir), makeTestFactoryPresetAssets());

    setParam (processor, ParamIDs::inTrim, 9.0f); // perturb first

    manager.applyStartupDefault();

    CHECK (manager.getCurrentPresetName() == "Default");
    CHECK (manager.isCurrentPresetFactory());
    CHECK (getParam (processor, ParamIDs::inTrim) == Catch::Approx (0.0f).margin (1.0e-3));
}

TEST_CASE ("PresetManager: a user Default preset wins over the factory Default", "[presets]")
{
    LancetAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    ScopedTestDirectory scratch;
    PresetManager manager (processor.apvts, makeIsolatedConfig (scratch.dir), makeTestFactoryPresetAssets());

    setParam (processor, ParamIDs::inTrim, 5.0f);
    REQUIRE (manager.setCurrentAsDefault()); // writes a user preset literally named "Default"

    setParam (processor, ParamIDs::inTrim, -3.0f); // perturb away before the resolution check

    manager.applyStartupDefault();

    CHECK (manager.getCurrentPresetName() == "Default");
    CHECK_FALSE (manager.isCurrentPresetFactory()); // resolved to the *user* Default, not the factory one
    CHECK (getParam (processor, ParamIDs::inTrim) == Catch::Approx (5.0f).margin (1.0e-3));
}

TEST_CASE ("PresetManager: resetDefault() removes the user Default so the factory Default resolves again", "[presets]")
{
    LancetAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    ScopedTestDirectory scratch;
    PresetManager manager (processor.apvts, makeIsolatedConfig (scratch.dir), makeTestFactoryPresetAssets());

    setParam (processor, ParamIDs::inTrim, 5.0f);
    REQUIRE (manager.setCurrentAsDefault());
    REQUIRE (manager.resetDefault());

    manager.applyStartupDefault();

    CHECK (manager.isCurrentPresetFactory());
    CHECK (getParam (processor, ParamIDs::inTrim) == Catch::Approx (0.0f).margin (1.0e-3));
}

//==============================================================================
// 6. Dirty flag: clean after load, dirty after any param change, clean after save.
TEST_CASE ("PresetManager: dirty flag lifecycle - clean after load, dirty after a change, clean after save", "[presets]")
{
    LancetAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    ScopedTestDirectory scratch;
    PresetManager manager (processor.apvts, makeIsolatedConfig (scratch.dir), makeTestFactoryPresetAssets());

    REQUIRE (manager.loadPreset ("Default"));
    CHECK_FALSE (manager.isDirty());

    setParam (processor, ParamIDs::inTrim, 3.0f);
    CHECK (manager.isDirty());

    REQUIRE (manager.saveUserPreset ("Dirty Flag Preset", "Init"));
    CHECK_FALSE (manager.isDirty());
}

//==============================================================================
// 7. prev/next ordering and wrap-around.
TEST_CASE ("PresetManager: nextPreset()/previousPreset() traverse alphabetically and wrap around", "[presets]")
{
    LancetAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    ScopedTestDirectory scratch;
    PresetManager manager (processor.apvts, makeIsolatedConfig (scratch.dir), makeTestFactoryPresetAssets());

    const auto all = manager.getAllPresets();
    REQUIRE (all.size() >= 2);

    REQUIRE (manager.loadPreset (all.front().name));

    manager.nextPreset();
    CHECK (manager.getCurrentPresetName() == all[1].name);

    manager.previousPreset();
    CHECK (manager.getCurrentPresetName() == all.front().name);

    // Wrap backward from the first entry to the last.
    manager.previousPreset();
    CHECK (manager.getCurrentPresetName() == all.back().name);

    // Wrap forward from the last entry back to the first.
    manager.nextPreset();
    CHECK (manager.getCurrentPresetName() == all.front().name);
}

//==============================================================================
// Additional coverage beyond the spec's minimum list: save/rename/delete
// guards, single-file export round-trip, and bank import/export.

TEST_CASE ("PresetManager: saveUserPreset() refuses to shadow a factory preset name", "[presets]")
{
    LancetAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    ScopedTestDirectory scratch;
    PresetManager manager (processor.apvts, makeIsolatedConfig (scratch.dir), makeTestFactoryPresetAssets());

    CHECK_FALSE (manager.saveUserPreset ("Default", "Init")); // "Default" already exists as a factory preset
    CHECK_FALSE (manager.saveUserPreset ("Gentle Glue", "Bus"));
}

TEST_CASE ("PresetManager: renameUserPreset() moves a user preset to a new name and preserves its parameters", "[presets]")
{
    LancetAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    ScopedTestDirectory scratch;
    PresetManager manager (processor.apvts, makeIsolatedConfig (scratch.dir), makeTestFactoryPresetAssets());

    setParam (processor, ParamIDs::b3Freq, 4321.0f);
    REQUIRE (manager.saveUserPreset ("Old Name", "Init"));

    REQUIRE (manager.renameUserPreset ("Old Name", "New Name"));

    setParam (processor, ParamIDs::b3Freq, 20000.0f); // perturb before reloading

    CHECK_FALSE (manager.loadPreset ("Old Name")); // gone
    REQUIRE (manager.loadPreset ("New Name"));
    CHECK (getParam (processor, ParamIDs::b3Freq) == Catch::Approx (4321.0f).margin (1.0e-1));
}

TEST_CASE ("PresetManager: deleteUserPreset() removes a user preset but never a factory preset", "[presets]")
{
    LancetAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    ScopedTestDirectory scratch;
    PresetManager manager (processor.apvts, makeIsolatedConfig (scratch.dir), makeTestFactoryPresetAssets());

    REQUIRE (manager.saveUserPreset ("Temporary", "Init"));
    REQUIRE (manager.deleteUserPreset ("Temporary"));
    CHECK_FALSE (manager.loadPreset ("Temporary"));

    // A factory preset name isn't a file on disk in the user directory, so
    // there's nothing to delete - deleteUserPreset() must return false, and
    // the factory preset must still load afterwards.
    CHECK_FALSE (manager.deleteUserPreset ("Default"));
    CHECK (manager.loadPreset ("Default"));
}

TEST_CASE ("PresetManager: exportPreset()/importPresetFile() single-file round-trip", "[presets]")
{
    LancetAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    ScopedTestDirectory scratch;
    PresetManager manager (processor.apvts, makeIsolatedConfig (scratch.dir), makeTestFactoryPresetAssets());

    setParam (processor, ParamIDs::b5Range, -7.0f);
    REQUIRE (manager.saveUserPreset ("Exportable", "Init"));

    const auto exportFile = juce::File::createTempFile (".basilicapreset");
    REQUIRE (manager.exportPreset ("Exportable", exportFile));
    REQUIRE (exportFile.existsAsFile());

    REQUIRE (manager.deleteUserPreset ("Exportable")); // remove the original before reimporting

    juce::String errorMessage;
    REQUIRE (manager.importPresetFile (exportFile, errorMessage));
    CHECK (getParam (processor, ParamIDs::b5Range) == Catch::Approx (-7.0f).margin (1.0e-3));

    exportFile.deleteFile();
}

TEST_CASE ("PresetManager: exportBank()/importBank() round-trips every user preset through a zip", "[presets]")
{
    ScopedTestDirectory sourceScratch;
    ScopedTestDirectory destScratch;

    LancetAudioProcessor sourceProcessor;
    sourceProcessor.prepareToPlay (48000.0, 512);
    PresetManager sourceManager (sourceProcessor.apvts, makeIsolatedConfig (sourceScratch.dir), makeTestFactoryPresetAssets());

    setParam (sourceProcessor, ParamIDs::b1Freq, 111.0f);
    REQUIRE (sourceManager.saveUserPreset ("Bank Preset A", "Init"));

    setParam (sourceProcessor, ParamIDs::b1Freq, 222.0f);
    REQUIRE (sourceManager.saveUserPreset ("Bank Preset B", "Init"));

    const auto bankFile = juce::File::createTempFile (".zip");
    REQUIRE (sourceManager.exportBank (bankFile));
    REQUIRE (bankFile.existsAsFile());

    LancetAudioProcessor destProcessor;
    destProcessor.prepareToPlay (48000.0, 512);
    PresetManager destManager (destProcessor.apvts, makeIsolatedConfig (destScratch.dir), makeTestFactoryPresetAssets());

    const auto importedCount = destManager.importBank (bankFile);
    CHECK (importedCount == 2);

    REQUIRE (destManager.loadPreset ("Bank Preset A"));
    CHECK (getParam (destProcessor, ParamIDs::b1Freq) == Catch::Approx (111.0f).margin (1.0e-1));

    REQUIRE (destManager.loadPreset ("Bank Preset B"));
    CHECK (getParam (destProcessor, ParamIDs::b1Freq) == Catch::Approx (222.0f).margin (1.0e-1));

    bankFile.deleteFile();
}

//==============================================================================
// 8. PresetManager never allocates or locks on the audio thread.
//
// Verified primarily *by design*: nothing in LancetAudioProcessor::
// processBlock()/LancetEngine ever calls into PresetManager (see
// PluginProcessor.cpp - presetManager is only touched from the constructor
// and from PresetBar's message-thread-only UI callbacks), so there is no
// code path for this test to exercise in the first place. The one nuance is
// PresetManager::parameterChanged() (an AudioProcessorValueTreeState::
// Listener callback that JUCE does not document as guaranteed message-
// thread-only) - it is implemented as a single lock-free std::atomic<bool>
// store and nothing else (see PresetManager.h/.cpp), which this test
// exercises indirectly by driving parameter changes and processBlock() back
// to back and confirming nothing misbehaves.
TEST_CASE ("PresetManager: parameter-driven dirty tracking coexists safely with real-time audio processing", "[presets]")
{
    LancetAudioProcessor processor;
    processor.prepareToPlay (48000.0, 256);

    ScopedTestDirectory scratch;
    PresetManager manager (processor.apvts, makeIsolatedConfig (scratch.dir), makeTestFactoryPresetAssets());

    REQUIRE (manager.loadPreset ("Default"));
    CHECK_FALSE (manager.isDirty());

    juce::AudioBuffer<float> buffer (2, 256);
    juce::MidiBuffer midi;

    for (int block = 0; block < 8; ++block)
    {
        setParam (processor, ParamIDs::b1Freq, 20.0f + static_cast<float> (block) * 10.0f);
        CHECK_NOTHROW (processor.processBlock (buffer, midi));
    }

    CHECK (manager.isDirty());
}

//==============================================================================
// v0.4.0 (SOTA brief §4): the eleventh factory preset, "Sidechain Carve",
// exists to make the new external-sidechain routing discoverable - a routing
// feature nobody can find is a feature nobody has. It is also the only shipped
// preset that sets either of the two new parameters away from index 0, so it
// doubles as a check that a preset carrying them actually applies them.
TEST_CASE ("PresetManager: the Sidechain Carve preset ships and applies the v0.4.0 detector routing", "[presets]")
{
    LancetAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    ScopedTestDirectory scratch;
    PresetManager manager (processor.apvts, makeIsolatedConfig (scratch.dir), makeTestFactoryPresetAssets());

    REQUIRE (manager.loadPreset ("Sidechain Carve"));
    CHECK (manager.isCurrentPresetFactory());

    auto* scSource = dynamic_cast<juce::AudioParameterChoice*> (processor.apvts.getParameter (ParamIDs::b3ScSource));
    REQUIRE (scSource != nullptr);
    CHECK (scSource->getIndex() == 1); // External

    auto* scMode = dynamic_cast<juce::AudioParameterChoice*> (processor.apvts.getParameter (ParamIDs::b3ScMode));
    REQUIRE (scMode != nullptr);
    CHECK (scMode->getIndex() == 1); // Wide

    auto* range = processor.apvts.getParameter (ParamIDs::b3Range);
    REQUIRE (range != nullptr);
    CHECK (range->convertFrom0to1 (range->getValue()) < 0.0f); // cuts as the sidechain gets loud

    auto* on = processor.apvts.getParameter (ParamIDs::b3On);
    REQUIRE (on != nullptr);
    CHECK (on->getValue() > 0.5f);
}

TEST_CASE ("PresetManager: every other factory preset leaves the v0.4.0 detector routing at its defaults",
           "[presets]")
{
    // The neutrality invariant applied to the shipped presets: the ten presets
    // that predate v0.4.0 must not have acquired new behaviour just because
    // new parameters exist. Only Sidechain Carve is allowed to move them.
    LancetAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    ScopedTestDirectory scratch;
    PresetManager manager (processor.apvts, makeIsolatedConfig (scratch.dir), makeTestFactoryPresetAssets());

    static constexpr const char* scIds[] = {
        ParamIDs::b1ScSource, ParamIDs::b2ScSource, ParamIDs::b3ScSource,
        ParamIDs::b4ScSource, ParamIDs::b5ScSource, ParamIDs::b6ScSource,
        ParamIDs::b1ScMode, ParamIDs::b2ScMode, ParamIDs::b3ScMode,
        ParamIDs::b4ScMode, ParamIDs::b5ScMode, ParamIDs::b6ScMode,
    };

    for (const auto& entry : manager.getAllPresets())
    {
        if (! entry.isFactory || entry.name == "Sidechain Carve")
            continue;

        CAPTURE (entry.name);
        REQUIRE (manager.loadPreset (entry.name));

        for (const auto* id : scIds)
        {
            CAPTURE (id);
            auto* param = dynamic_cast<juce::AudioParameterChoice*> (processor.apvts.getParameter (id));
            REQUIRE (param != nullptr);
            CHECK (param->getIndex() == 0);
        }
    }
}

//==============================================================================
// Vendor identity (basilica-audio/.github#2, ADR 0001): user presets moved from
// the `Yves Vogl` manufacturer folder to `Basilica Audio`, and a user must not
// lose a preset over it. These cases pin the migration's whole contract - it
// adopts, it copies rather than moves, it never overwrites, it stays out of the
// real per-user folder during tests, and both folder shapes match the platform
// convention (asserted on whichever platform is running, so macOS and Windows
// CI each check their own).
namespace
{
    // Writes a preset document straight to disk rather than going through
    // PresetManager::saveUserPreset(), so the migration is exercised against a
    // file shaped like one an older build left behind - not one this build
    // happened to produce a moment earlier.
    void writeBrandingLegacyPresetFile (const juce::File& directory,
                                const juce::String& presetName,
                                const juce::String& category,
                                const juce::String& pluginId)
    {
        directory.createDirectory();

        auto* preset = new juce::DynamicObject();
        preset->setProperty ("format", PresetManager::presetFormatTag);
        preset->setProperty ("plugin", pluginId);
        preset->setProperty ("pluginVersion", "0.1.0-legacy");
        preset->setProperty ("name", presetName);
        preset->setProperty ("category", category);
        preset->setProperty ("parameters", juce::var (new juce::DynamicObject()));

        const auto written = directory
            .getChildFile (juce::File::createLegalFileName (presetName)
                            + PresetManager::presetFileExtension)
            .replaceWithText (juce::JSON::toString (juce::var (preset), false));

        REQUIRE (written);
    }

    bool brandingContainsUserPreset (const std::vector<PresetManager::PresetEntry>& entries,
                             const juce::String& name)
    {
        return std::any_of (entries.begin(), entries.end(),
                            [&name] (const PresetManager::PresetEntry& entry)
                            { return entry.name == name && ! entry.isFactory; });
    }

    juce::String brandingCategoryOf (const std::vector<PresetManager::PresetEntry>& entries,
                             const juce::String& name)
    {
        for (auto& entry : entries)
            if (entry.name == name)
                return entry.category;

        return {};
    }
}

TEST_CASE ("PresetManager: a preset saved under the legacy manufacturer folder still loads", "[presets][branding]")
{
    LancetAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    ScopedTestDirectory legacyDirectory;
    ScopedTestDirectory currentDirectory;

    auto config = makeIsolatedConfig (currentDirectory.dir);
    config.legacyManufacturerName = "Yves Vogl";
    config.legacyUserPresetsDirectoryOverrideForTests = legacyDirectory.dir;

    writeBrandingLegacyPresetFile (legacyDirectory.dir, "Legacy Preset", "User", config.pluginId);

    PresetManager manager (processor.apvts, config, makeTestFactoryPresetAssets());

    REQUIRE (brandingContainsUserPreset (manager.getAllPresets(), "Legacy Preset"));
    REQUIRE (manager.loadPreset ("Legacy Preset"));
    REQUIRE (manager.getCurrentPresetName() == juce::String ("Legacy Preset"));
    REQUIRE_FALSE (manager.isCurrentPresetFactory());

    const auto fileName = juce::String ("Legacy Preset") + PresetManager::presetFileExtension;

    // Copied, not moved: an older build of this plugin - or a downgrade - still
    // finds its own presets exactly where it left them.
    REQUIRE (legacyDirectory.dir.getChildFile (fileName).existsAsFile());
    REQUIRE (currentDirectory.dir.getChildFile (fileName).existsAsFile());
}

TEST_CASE ("PresetManager: the legacy migration never overwrites a preset already in the new folder", "[presets][branding]")
{
    LancetAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    ScopedTestDirectory legacyDirectory;
    ScopedTestDirectory currentDirectory;

    auto config = makeIsolatedConfig (currentDirectory.dir);
    config.legacyManufacturerName = "Yves Vogl";
    config.legacyUserPresetsDirectoryOverrideForTests = legacyDirectory.dir;

    writeBrandingLegacyPresetFile (legacyDirectory.dir, "Shared Name", "From Legacy", config.pluginId);
    writeBrandingLegacyPresetFile (currentDirectory.dir, "Shared Name", "From Current", config.pluginId);

    PresetManager manager (processor.apvts, config, makeTestFactoryPresetAssets());

    REQUIRE (brandingCategoryOf (manager.getAllPresets(), "Shared Name") == juce::String ("From Current"));

    // Idempotent: constructing a second manager over the same pair of folders
    // must not suddenly prefer the legacy copy either.
    PresetManager second (processor.apvts, config, makeTestFactoryPresetAssets());
    REQUIRE (brandingCategoryOf (second.getAllPresets(), "Shared Name") == juce::String ("From Current"));
}

TEST_CASE ("PresetManager: overriding only the current preset directory disables the legacy lookup", "[presets][branding]")
{
    ScopedTestDirectory currentDirectory;

    auto config = makeIsolatedConfig (currentDirectory.dir);
    config.legacyManufacturerName = "Yves Vogl";

    // Without this, a test that redirects only the current directory would read
    // - and copy from - the real presets of whoever is running the suite.
    REQUIRE (PresetManager::getLegacyUserPresetsDirectory (config) == juce::File());
}

TEST_CASE ("PresetManager: current and legacy preset folders follow the platform convention", "[presets][branding]")
{
    PresetManagerConfig config;
    config.pluginName = "Lancet";
    config.manufacturerName = "Basilica Audio";
    config.legacyManufacturerName = "Yves Vogl";

    const auto current = PresetManager::getUserPresetsDirectory (config);
    const auto legacy = PresetManager::getLegacyUserPresetsDirectory (config);

   #if JUCE_MAC
    const auto presetsRoot = juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                                 .getChildFile ("Library")
                                 .getChildFile ("Audio")
                                 .getChildFile ("Presets");

    REQUIRE (current == presetsRoot.getChildFile ("Basilica Audio").getChildFile ("Lancet"));
    REQUIRE (legacy == presetsRoot.getChildFile ("Yves Vogl").getChildFile ("Lancet"));
   #else
    const auto applicationData = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);

    REQUIRE (current == applicationData.getChildFile ("Basilica Audio")
                            .getChildFile ("Lancet").getChildFile ("Presets"));
    REQUIRE (legacy == applicationData.getChildFile ("Yves Vogl")
                            .getChildFile ("Lancet").getChildFile ("Presets"));
   #endif

    // The two are the same path shape and differ only in the manufacturer
    // component - which is what makes "copy from legacy to current" a rename of
    // one folder rather than a move between two unrelated layouts.
    REQUIRE (current != legacy);
    REQUIRE (current.getFileName() == legacy.getFileName());
}

TEST_CASE ("PresetManager: an empty legacy manufacturer name disables the migration", "[presets][branding]")
{
    PresetManagerConfig config;
    config.pluginName = "Lancet";
    config.manufacturerName = "Basilica Audio";

    REQUIRE (PresetManager::getLegacyUserPresetsDirectory (config) == juce::File());

    // And so does a legacy name that has already caught up with the current one,
    // so re-running a completed rename is a no-op rather than a self-copy.
    config.legacyManufacturerName = "Basilica Audio";
    REQUIRE (PresetManager::getLegacyUserPresetsDirectory (config) == juce::File());
}

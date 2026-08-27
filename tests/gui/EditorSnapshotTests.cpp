#include "PluginEditor.h"
#include "PluginProcessor.h"

#include <BinaryData.h>

#include <catch2/catch_test_macros.hpp>

#include <map>

// GUI smoke + snapshot tests for the wave-3 composited-plate editor.
// juce::ScopedJuceInitialiser_GUI is installed once for the whole test
// binary in tests/TestMain.cpp, so Components are safe to construct in this
// headless console executable (timers never fire; the preview hooks seed
// live-looking state instead).
namespace
{
    basilica::gui::NeedleDial* findDialByTitle (juce::Component& parent, const juce::String& title)
    {
        for (int i = 0; i < parent.getNumChildComponents(); ++i)
            if (auto* dial = dynamic_cast<basilica::gui::NeedleDial*> (parent.getChildComponent (i)))
                if (dial->getTitle() == title)
                    return dial;

        return nullptr;
    }

    juce::Slider* findSliderByName (juce::Component& parent, const juce::String& name)
    {
        for (int i = 0; i < parent.getNumChildComponents(); ++i)
            if (auto* slider = dynamic_cast<juce::Slider*> (parent.getChildComponent (i)))
                if (slider->getName() == name)
                    return slider;

        return nullptr;
    }

    // Live-looking state for the committed preview: needles deflected to
    // visibly different readings, a few knobs off their defaults, a lamp on.
    void configureLiveLookingState (LancetAudioProcessor& processor, LancetAudioProcessorEditor& editor)
    {
        const std::pair<int, float> dialPoses[] = {
            { 1, -2.0f }, { 2, -6.0f }, { 3, -0.5f }, { 4, -10.0f }, { 5, -4.0f }, { 6, -1.0f },
        };

        for (const auto& [band, db] : dialPoses)
            if (auto* dial = findDialByTitle (editor, "Band " + juce::String (band) + " gain reduction meter"))
                dial->setImmediateDbForPreview (db);

        const std::pair<const char*, double> knobPoses[] = {
            { "in_trim", 0.55 }, { "mix", 0.9 }, { "b1_gain", 0.3 },
            { "b2_freq", 0.62 }, { "b4_thresh", 0.35 }, { "b6_gain", 0.75 },
        };

        for (const auto& [id, proportion] : knobPoses)
            if (auto* slider = findSliderByName (editor, id))
                slider->setValue (slider->proportionOfLengthToValue (proportion), juce::dontSendNotification);

        if (auto* listen = processor.apvts.getParameter ("b3_listen"))
            listen->setValueNotifyingHost (1.0f);
    }
}

TEST_CASE ("Editor constructs, lays out, and destroys cleanly", "[gui]")
{
    LancetAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    {
        LancetAudioProcessorEditor editor (processor);

        CHECK (editor.getWidth() == editor.getDesignWidth());
        CHECK (editor.getHeight() == editor.getDesignHeight());
    }
    // Destroyed here - JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR asserts
    // at process exit in Debug if any tagged instance leaked.
}

TEST_CASE ("The editor renders a non-blank snapshot and writes the committed preview", "[gui][preview]")
{
    LancetAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    LancetAudioProcessorEditor editor (processor);
    configureLiveLookingState (processor, editor);

    // SoftwareImageType: no native graphics context needed, robust on
    // headless CI runners.
    const auto snapshot = editor.createComponentSnapshot (editor.getLocalBounds(), true, 1.0f,
                                                          juce::SoftwareImageType {});

    REQUIRE (snapshot.isValid());
    CHECK (snapshot.getWidth() == editor.getWidth());
    CHECK (snapshot.getHeight() == editor.getHeight());

    // Non-blank, non-flat: count distinct colours over a coarse grid.
    std::map<juce::uint32, int> histogram;

    for (int y = 0; y < snapshot.getHeight(); y += juce::jmax (1, snapshot.getHeight() / 24))
        for (int x = 0; x < snapshot.getWidth(); x += juce::jmax (1, snapshot.getWidth() / 24))
            ++histogram[snapshot.getPixelAt (x, y).getARGB()];

    CHECK (histogram.size() > 24);

#ifdef LANCET_REPO_ROOT
    // Committed for PR review as docs/gui-preview.png (suite convention:
    // the preview is GENERATED from the real editor, never mocked).
    juce::PNGImageFormat png;
    const auto outFile = juce::File (LANCET_REPO_ROOT).getChildFile ("docs").getChildFile ("gui-preview.png");

    if (auto stream = std::unique_ptr<juce::FileOutputStream> (outFile.createOutputStream()))
    {
        stream->setPosition (0);
        stream->truncate();
        CHECK (png.writeImageToStream (snapshot, *stream));
    }
    else
    {
        FAIL ("could not open output stream for " << outFile.getFullPathName());
    }
#endif
}

TEST_CASE ("The two VU needles render visibly different poses at different readings", "[gui][metering]")
{
    LancetAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    LancetAudioProcessorEditor editor (processor);

    auto* input = findDialByTitle (editor, "Band 1 gain reduction meter");
    auto* output = findDialByTitle (editor, "Band 6 gain reduction meter");

    REQUIRE (input != nullptr);
    REQUIRE (output != nullptr);

    input->setImmediateDbForPreview (-18.0f);
    output->setImmediateDbForPreview (0.0f);

    const auto snapshotOf = [] (juce::Component& component)
    {
        return component.createComponentSnapshot (component.getLocalBounds(), false, 1.0f,
                                                  juce::SoftwareImageType {});
    };

    const auto restPose = snapshotOf (*input);
    const auto hotPose = snapshotOf (*output);

    REQUIRE (restPose.isValid());
    REQUIRE (hotPose.isValid());
    REQUIRE (restPose.getWidth() == hotPose.getWidth());

    int differing = 0;

    // Full-resolution sampling: the D2 mini dials are only ~52 px at 1x,
    // so a strided walk would see too few needle pixels to discriminate.
    for (int y = 0; y < restPose.getHeight(); ++y)
        for (int x = 0; x < restPose.getWidth(); ++x)
            if (restPose.getPixelAt (x, y) != hotPose.getPixelAt (x, y))
                ++differing;

    // The faces are identical sprites - any difference is the needle.
    CHECK (differing > 25);
}

TEST_CASE ("Plate typography brightens the label zones over the raw plate", "[gui][a11y]")
{
    LancetAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    LancetAudioProcessorEditor editor (processor);

    const auto snapshot = editor.createComponentSnapshot (editor.getLocalBounds(), true, 1.0f,
                                                          juce::SoftwareImageType {});
    REQUIRE (snapshot.isValid());

    const auto plate = juce::ImageCache::getFromMemory (BinaryData::plate_lancet_png,
                                                        BinaryData::plate_lancet_pngSize);
    REQUIRE (plate.isValid());

    const auto manifest = LancetAudioProcessorEditor::parseLayoutManifest();

    const auto brightFraction = [] (const juce::Image& image, juce::Rectangle<int> area, int threshold)
    {
        int bright = 0, total = 0;

        for (int y = juce::jmax (0, area.getY()); y < juce::jmin (image.getHeight(), area.getBottom()); ++y)
        {
            for (int x = juce::jmax (0, area.getX()); x < juce::jmin (image.getWidth(), area.getRight()); ++x)
            {
                const auto c = image.getPixelAt (x, y);
                const auto lum = (int) std::lround (0.299f * c.getRed() + 0.587f * c.getGreen() + 0.114f * c.getBlue());
                ++total;

                if (lum > threshold)
                    ++bright;
            }
        }

        return total > 0 ? (float) bright / (float) total : 0.0f;
    };

    int checkedLabels = 0;

    for (const auto& label : manifest.labels)
    {
        if (label.style != "section" && label.style != "wordmark")
            continue;

        const auto w = 150;
        const auto snapshotArea = juce::Rectangle<int> ((int) label.cx - w / 2,
                                                        (int) label.cy - (int) label.h / 2
                                                            + LancetAudioProcessorEditor::topStripHeight1x,
                                                        w, (int) label.h + 4);
        // The plate asset is a 2k render drawn at plateWidth1x - map the
        // 1x box into the asset's own pixel space before sampling it.
        const auto plateScale = (float) plate.getWidth() / (float) manifest.plateWidth1x;
        const auto plateArea1x = snapshotArea.translated (0, -LancetAudioProcessorEditor::topStripHeight1x);
        const auto plateArea = juce::Rectangle<int> ((int) ((float) plateArea1x.getX() * plateScale),
                                                     (int) ((float) plateArea1x.getY() * plateScale),
                                                     (int) ((float) plateArea1x.getWidth() * plateScale),
                                                     (int) ((float) plateArea1x.getHeight() * plateScale));

        // Gold lettering against the near-black plate: the snapshot's label
        // box must contain clearly more bright pixels than the raw plate's
        // same box.
        CHECK (brightFraction (snapshot, snapshotArea, 110)
               > brightFraction (plate, plateArea, 110) + 0.01f);
        ++checkedLabels;
    }

    CHECK (checkedLabels >= 4);
}

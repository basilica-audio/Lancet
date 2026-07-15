#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "params/ParameterIds.h"

namespace
{
    constexpr int knobSize = 62;
    constexpr int textBoxHeight = 16;
    constexpr int labelHeight = 16;
    constexpr int bandLabelHeight = 20;
    constexpr int toggleRowHeight = 24;
    constexpr int comboRowHeight = 22;
    constexpr int margin = 12;
    constexpr int numColumns = LancetEngine::numBands;
    constexpr int rowHeight = labelHeight + knobSize + textBoxHeight;
    constexpr int numKnobRows = 7; // Freq, Q, Gain, Range, Threshold, Attack, Release

    constexpr int editorWidth = margin * 2 + numColumns * knobSize + (numColumns + 1) * margin;

    constexpr int editorHeight = margin + rowHeight // top strip (In Trim/Out Trim/Mix)
                                  + margin + bandLabelHeight + toggleRowHeight + comboRowHeight
                                  + numKnobRows * rowHeight + margin;

    // String IDs for one band's parameters, mirroring
    // src/PluginProcessor.cpp's own (anonymous-namespace) bandIds table.
    struct BandIdSet
    {
        const char* on;
        const char* type; // nullptr for bands 2-5
        const char* freq;
        const char* q;
        const char* gain;
        const char* range;
        const char* threshold;
        const char* attack;
        const char* release;
        const char* listen;
    };

    constexpr std::array<BandIdSet, LancetEngine::numBands> bandIds { {
        { ParamIDs::b1On, ParamIDs::b1Type, ParamIDs::b1Freq, ParamIDs::b1Q, ParamIDs::b1Gain,
          ParamIDs::b1Range, ParamIDs::b1Threshold, ParamIDs::b1Attack, ParamIDs::b1Release, ParamIDs::b1Listen },
        { ParamIDs::b2On, nullptr, ParamIDs::b2Freq, ParamIDs::b2Q, ParamIDs::b2Gain,
          ParamIDs::b2Range, ParamIDs::b2Threshold, ParamIDs::b2Attack, ParamIDs::b2Release, ParamIDs::b2Listen },
        { ParamIDs::b3On, nullptr, ParamIDs::b3Freq, ParamIDs::b3Q, ParamIDs::b3Gain,
          ParamIDs::b3Range, ParamIDs::b3Threshold, ParamIDs::b3Attack, ParamIDs::b3Release, ParamIDs::b3Listen },
        { ParamIDs::b4On, nullptr, ParamIDs::b4Freq, ParamIDs::b4Q, ParamIDs::b4Gain,
          ParamIDs::b4Range, ParamIDs::b4Threshold, ParamIDs::b4Attack, ParamIDs::b4Release, ParamIDs::b4Listen },
        { ParamIDs::b5On, nullptr, ParamIDs::b5Freq, ParamIDs::b5Q, ParamIDs::b5Gain,
          ParamIDs::b5Range, ParamIDs::b5Threshold, ParamIDs::b5Attack, ParamIDs::b5Release, ParamIDs::b5Listen },
        { ParamIDs::b6On, ParamIDs::b6Type, ParamIDs::b6Freq, ParamIDs::b6Q, ParamIDs::b6Gain,
          ParamIDs::b6Range, ParamIDs::b6Threshold, ParamIDs::b6Attack, ParamIDs::b6Release, ParamIDs::b6Listen },
    } };
}

LancetAudioProcessorEditor::LancetAudioProcessorEditor (LancetAudioProcessor& processorToEdit)
    : juce::AudioProcessorEditor (&processorToEdit),
      audioProcessor (processorToEdit)
{
    configureKnob (inTrimKnob, ParamIDs::inTrim, "In Trim");
    configureKnob (outTrimKnob, ParamIDs::outTrim, "Out Trim");
    configureKnob (mixKnob, ParamIDs::mix, "Mix");

    static constexpr const char* bandNames[LancetEngine::numBands] = {
        "Band 1", "Band 2", "Band 3", "Band 4", "Band 5", "Band 6"
    };
    static constexpr const char* shelfLabels[LancetEngine::numBands] = {
        "Low Shelf", "", "", "", "", "High Shelf"
    };

    for (int i = 0; i < LancetEngine::numBands; ++i)
    {
        configureBandLabel (bandLabels[static_cast<size_t> (i)], bandNames[i]);
        configureBand (bandControls[static_cast<size_t> (i)], i, shelfLabels[i]);
    }

    setResizable (false, false);
    setSize (editorWidth, editorHeight);
}

LancetAudioProcessorEditor::~LancetAudioProcessorEditor() = default;

void LancetAudioProcessorEditor::configureKnob (Knob& knob, const juce::String& parameterId, const juce::String& labelText)
{
    knob.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, knobSize, textBoxHeight);
    addAndMakeVisible (knob.slider);

    knob.label.setText (labelText, juce::dontSendNotification);
    knob.label.setJustificationType (juce::Justification::centred);
    knob.label.setFont (juce::Font (juce::FontOptions (11.0f)));
    // false => label sits above the slider it tracks; JUCE repositions it
    // automatically whenever the slider's bounds change, so resized() only
    // needs to place the sliders themselves.
    knob.label.attachToComponent (&knob.slider, false);
    addAndMakeVisible (knob.label);

    knob.attachment = std::make_unique<SliderAttachment> (audioProcessor.apvts, parameterId, knob.slider);
}

void LancetAudioProcessorEditor::configureToggle (Toggle& toggle, const juce::String& parameterId, const juce::String& labelText)
{
    toggle.button.setButtonText (labelText);
    addAndMakeVisible (toggle.button);

    toggle.attachment = std::make_unique<ButtonAttachment> (audioProcessor.apvts, parameterId, toggle.button);
}

void LancetAudioProcessorEditor::configureBandLabel (juce::Label& label, const juce::String& text)
{
    label.setText (text, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.setFont (juce::Font (juce::FontOptions (14.0f, juce::Font::bold)));
    addAndMakeVisible (label);
}

void LancetAudioProcessorEditor::configureBand (BandControls& band, int bandIndex, const juce::String& shelfLabel)
{
    const auto& ids = bandIds[static_cast<size_t> (bandIndex)];

    configureToggle (band.on, ids.on, "On");
    configureToggle (band.listen, ids.listen, "Listen");

    band.hasType = ids.type != nullptr;

    if (band.hasType)
    {
        band.typeBox.addItem ("Bell", 1);
        band.typeBox.addItem (shelfLabel, 2);
        addAndMakeVisible (band.typeBox);
        band.typeAttachment = std::make_unique<ComboBoxAttachment> (audioProcessor.apvts, ids.type, band.typeBox);
    }
    else
    {
        band.typeBox.setVisible (false);
    }

    configureKnob (band.freq, ids.freq, "Freq");
    configureKnob (band.q, ids.q, "Q");
    configureKnob (band.gain, ids.gain, "Gain");
    configureKnob (band.range, ids.range, "Range");
    configureKnob (band.threshold, ids.threshold, "Thresh");
    configureKnob (band.attack, ids.attack, "Attack");
    configureKnob (band.release, ids.release, "Release");
}

void LancetAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds().reduced (margin);

    // Top strip: In Trim, Out Trim, Mix - three knobs, left-aligned, not
    // stretched across the full band-column grid width below.
    auto topRow = bounds.removeFromTop (rowHeight);
    const auto topSlotWidth = knobSize + margin;

    inTrimKnob.slider.setBounds (topRow.removeFromLeft (topSlotWidth).reduced (margin / 2, 0));
    outTrimKnob.slider.setBounds (topRow.removeFromLeft (topSlotWidth).reduced (margin / 2, 0));
    mixKnob.slider.setBounds (topRow.removeFromLeft (topSlotWidth).reduced (margin / 2, 0));

    bounds.removeFromTop (margin);

    const auto columnWidth = bounds.getWidth() / numColumns;

    for (int i = 0; i < LancetEngine::numBands; ++i)
    {
        auto column = bounds.removeFromLeft (columnWidth);
        auto& band = bandControls[static_cast<size_t> (i)];

        bandLabels[static_cast<size_t> (i)].setBounds (column.removeFromTop (bandLabelHeight));

        auto toggleRow = column.removeFromTop (toggleRowHeight);
        const auto half = toggleRow.getWidth() / 2;
        band.on.button.setBounds (toggleRow.removeFromLeft (half).reduced (margin / 4, 2));
        band.listen.button.setBounds (toggleRow.reduced (margin / 4, 2));

        auto comboRow = column.removeFromTop (comboRowHeight);
        if (band.hasType)
            band.typeBox.setBounds (comboRow.reduced (margin / 4, 2));

        for (auto* knob : { &band.freq, &band.q, &band.gain, &band.range, &band.threshold, &band.attack, &band.release })
            knob->slider.setBounds (column.removeFromTop (rowHeight).reduced (margin / 2, 0));
    }
}

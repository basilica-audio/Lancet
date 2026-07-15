#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "dsp/LancetEngine.h"

#include <array>
#include <memory>

class LancetAudioProcessor;

// A simple, functional v0.1 editor: a top strip of Input Trim/Output Trim/
// Mix knobs above six per-band columns (Band 1 - Band 6, signal-flow
// order), each holding an On/Listen toggle row, a Type combo (Bell/Shelf -
// Band 1 and Band 6 only, per docs/design-brief.md), and Freq/Q/Gain/Range/
// Threshold/Attack/Release knobs bound via SliderAttachment/
// ButtonAttachment/ComboBoxAttachment. A custom vector-drawn GUI (readable
// control state, gain-reduction needles) is a later milestone (M3, see
// CLAUDE.md); this is deliberately plain but fully wired and usable.
class LancetAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit LancetAudioProcessorEditor (LancetAudioProcessor& processorToEdit);
    ~LancetAudioProcessorEditor() override;

    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<SliderAttachment> attachment;
    };

    struct Toggle
    {
        juce::ToggleButton button;
        std::unique_ptr<ButtonAttachment> attachment;
    };

    // One band's full control set, in signal-flow order.
    struct BandControls
    {
        Toggle on;
        Toggle listen;
        juce::ComboBox typeBox; // only used/visible for Band 1 (LowShelf) and Band 6 (HighShelf)
        std::unique_ptr<ComboBoxAttachment> typeAttachment;
        bool hasType = false;

        Knob freq;
        Knob q;
        Knob gain;
        Knob range;
        Knob threshold;
        Knob attack;
        Knob release;
    };

    void configureKnob (Knob& knob, const juce::String& parameterId, const juce::String& labelText);
    void configureToggle (Toggle& toggle, const juce::String& parameterId, const juce::String& labelText);
    void configureBand (BandControls& band, int bandIndex, const juce::String& shelfLabel);
    void configureBandLabel (juce::Label& label, const juce::String& text);

    LancetAudioProcessor& audioProcessor;

    // Top strip: global trim/mix.
    Knob inTrimKnob;
    Knob outTrimKnob;
    Knob mixKnob;

    std::array<juce::Label, LancetEngine::numBands> bandLabels;
    std::array<BandControls, LancetEngine::numBands> bandControls;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LancetAudioProcessorEditor)
};

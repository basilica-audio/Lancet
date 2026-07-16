#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "dsp/LancetEngine.h"
#include "presets/PresetBar.h"

#include <array>
#include <memory>

class LancetAudioProcessor;

// A simple, functional v0.1/v0.2 editor: a preset bar (M2, docs/design-
// brief.md's preset system) docked at the top, above a strip of Input Trim/
// Output Trim/Mix knobs and six per-band columns (Band 1 - Band 6,
// signal-flow order), each holding an On/Listen toggle row, a Type combo
// (Bell/Shelf - Band 1 and Band 6 only, per docs/design-brief.md), and
// Freq/Q/Gain/Range/Threshold/Attack/Release knobs bound via
// SliderAttachment/ButtonAttachment/ComboBoxAttachment. The two new v0.2.0
// per-band booleans (`bN_autoRelease`/`bN_gainQ`) have no dedicated control
// yet - deliberately deferred to M3 alongside the rest of the custom
// LookAndFeel pass (see docs/design-brief.md §7); they remain fully
// automation/preset-controllable in the meantime. A custom vector-drawn GUI
// (readable control state, gain-reduction needles) is a later milestone
// (M3, see CLAUDE.md); this is deliberately plain but fully wired and
// usable.
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

    // M2 preset system (src/presets/PresetBar.h) - a horizontal strip
    // docked at the top of the editor. Constructed after the localisation
    // frame is installed (see the constructor) so its TRANS()'d strings
    // (and any of its own dialogs opened later) pick up the right language
    // from the very first paint.
    basilica::presets::PresetBar presetBar;

    // Top strip: global trim/mix.
    Knob inTrimKnob;
    Knob outTrimKnob;
    Knob mixKnob;

    std::array<juce::Label, LancetEngine::numBands> bandLabels;
    std::array<BandControls, LancetEngine::numBands> bandControls;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LancetAudioProcessorEditor)
};

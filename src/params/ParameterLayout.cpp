#include "ParameterLayout.h"
#include "ParameterIds.h"

namespace
{
    // True logarithmic (base-10) mapping, so slider/knob travel spends equal
    // space per octave/decade rather than per linear unit. Used for
    // frequency (Hz) and time-constant (ms) parameters, both perceived
    // logarithmically. Uses juce::mapToLog10/mapFromLog10 rather than
    // NormalisableRange's built-in power-law skew, which only approximates
    // a log curve. Matches sibling plugins' ParameterLayout.cpp convention
    // (e.g. triptych, overture).
    juce::NormalisableRange<float> makeLogRange (float rangeMin, float rangeMax)
    {
        return juce::NormalisableRange<float> (
            rangeMin,
            rangeMax,
            [] (float start, float end, float normalised)
            { return juce::mapToLog10 (normalised, start, end); },
            [] (float start, float end, float value)
            { return juce::mapFromLog10 (value, start, end); });
    }

    // String IDs for one band's full parameter set. `type` is null for
    // bands 2-5, which don't expose a Bell/Shelf choice (see
    // docs/design-brief.md: only Band 1/Band 6 expose Shelf).
    struct BandIds
    {
        const char* on;
        const char* type;
        const char* freq;
        const char* q;
        const char* gain;
        const char* range;
        const char* threshold;
        const char* attack;
        const char* release;
        const char* listen;
        const char* autoRelease;
        const char* gainQ;
    };

    // Adds one band's full parameter set. `shelfLabel` names which shelf
    // type Band 1/6's Type choice exposes ("Low Shelf"/"High Shelf"); pass
    // an empty label for bands 2-5, which get no Type parameter at all
    // (fixed Bell).
    void addBandParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout,
                             const BandIds& ids,
                             const juce::String& labelPrefix,
                             float defaultFreqHz,
                             bool defaultOn,
                             const juce::String& shelfLabel)
    {
        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { ids.on, 1 }, labelPrefix + " On", defaultOn));

        if (ids.type != nullptr)
        {
            layout.add (std::make_unique<juce::AudioParameterChoice> (
                juce::ParameterID { ids.type, 1 },
                labelPrefix + " Type",
                juce::StringArray { "Bell", shelfLabel },
                0)); // default Bell
        }

        // Frequency: 20 Hz - 20 kHz, log-skewed.
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ids.freq, 1 },
            labelPrefix + " Freq",
            makeLogRange (20.0f, 20000.0f),
            defaultFreqHz,
            juce::AudioParameterFloatAttributes().withLabel ("Hz")));

        // Q: 0.3 - 12, default 1.0. Ignored in Shelf mode (fixed 0.707 -
        // see DynamicBand.cpp).
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ids.q, 1 },
            labelPrefix + " Q",
            juce::NormalisableRange<float> (0.3f, 12.0f, 0.001f, 0.4f), // skewed toward the musically common low-Q end
            1.0f));

        // Static gain: +-12 dB, default 0.
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ids.gain, 1 },
            labelPrefix + " Gain",
            juce::NormalisableRange<float> (-12.0f, 12.0f, 0.01f),
            0.0f,
            juce::AudioParameterFloatAttributes().withLabel ("dB")));

        // Dynamic depth: +-12 dB, default 0 (0 = static band, dynamics
        // disabled - see docs/design-brief.md).
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ids.range, 1 },
            labelPrefix + " Range",
            juce::NormalisableRange<float> (-12.0f, 12.0f, 0.01f),
            0.0f,
            juce::AudioParameterFloatAttributes().withLabel ("dB")));

        // Detector threshold: -60 - 0 dB, default -30 dB.
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ids.threshold, 1 },
            labelPrefix + " Threshold",
            juce::NormalisableRange<float> (-60.0f, 0.0f, 0.01f),
            -30.0f,
            juce::AudioParameterFloatAttributes().withLabel ("dB")));

        // Attack: 0.1 - 500 ms, default 5 ms. v0.2.0 widened both ends
        // (floor 0.5 -> 0.1 ms, ceiling 100 -> 500 ms) per
        // docs/design-brief.md - the v0.1 range was a strict subset, so no
        // clamping is needed for any pre-existing session value on tolerant
        // import.
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ids.attack, 1 },
            labelPrefix + " Attack",
            makeLogRange (0.1f, 500.0f),
            5.0f,
            juce::AudioParameterFloatAttributes().withLabel ("ms")));

        // Release: 5 - 1500 ms, default 150 ms. v0.2.0 widened both ends
        // (floor 10 -> 5 ms, ceiling 1000 -> 1500 ms) per
        // docs/design-brief.md - same tolerant-import subset property as
        // Attack above.
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ids.release, 1 },
            labelPrefix + " Release",
            makeLogRange (5.0f, 1500.0f),
            150.0f,
            juce::AudioParameterFloatAttributes().withLabel ("ms")));

        // Listen (exclusive sidechain solo): off by default so adding it
        // never changes existing default behaviour.
        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { ids.listen, 1 }, labelPrefix + " Listen", false));

        // Program-dependent auto-release (v0.2.0, docs/design-brief.md §2/§3):
        // off by default for every band - required for the tolerant-import
        // guarantee (a v0.1.0 session must sound identical after import) and
        // for the plain fixed-Release-ms behaviour to remain the default.
        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { ids.autoRelease, 1 }, labelPrefix + " Auto Release", false));

        // Gain/Q coupling (v0.2.0, docs/design-brief.md §2/§3): off by
        // default for every band - an opt-in analog-style softening
        // character switch, not a forced behavioural change to existing
        // bands.
        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { ids.gainQ, 1 }, labelPrefix + " Gain/Q", false));
    }
}

namespace lnct
{
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;

        //======================================================================
        // Global input trim, applied before Band 1.
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::inTrim, 1 },
            "Input Trim",
            juce::NormalisableRange<float> (-12.0f, 12.0f, 0.01f),
            0.0f,
            juce::AudioParameterFloatAttributes().withLabel ("dB")));

        //======================================================================
        // Six bands in signal-flow order. Band 3 defaults on (a musically
        // sane "hear it working" out-of-the-box state); every other band
        // defaults off, matching docs/design-brief.md.
        addBandParameters (layout,
                            { ParamIDs::b1On, ParamIDs::b1Type, ParamIDs::b1Freq, ParamIDs::b1Q, ParamIDs::b1Gain,
                              ParamIDs::b1Range, ParamIDs::b1Threshold, ParamIDs::b1Attack, ParamIDs::b1Release, ParamIDs::b1Listen,
                              ParamIDs::b1AutoRelease, ParamIDs::b1GainQ },
                            "Band 1", 100.0f, false, "Low Shelf");

        addBandParameters (layout,
                            { ParamIDs::b2On, nullptr, ParamIDs::b2Freq, ParamIDs::b2Q, ParamIDs::b2Gain,
                              ParamIDs::b2Range, ParamIDs::b2Threshold, ParamIDs::b2Attack, ParamIDs::b2Release, ParamIDs::b2Listen,
                              ParamIDs::b2AutoRelease, ParamIDs::b2GainQ },
                            "Band 2", 250.0f, false, {});

        addBandParameters (layout,
                            { ParamIDs::b3On, nullptr, ParamIDs::b3Freq, ParamIDs::b3Q, ParamIDs::b3Gain,
                              ParamIDs::b3Range, ParamIDs::b3Threshold, ParamIDs::b3Attack, ParamIDs::b3Release, ParamIDs::b3Listen,
                              ParamIDs::b3AutoRelease, ParamIDs::b3GainQ },
                            "Band 3", 630.0f, true, {});

        addBandParameters (layout,
                            { ParamIDs::b4On, nullptr, ParamIDs::b4Freq, ParamIDs::b4Q, ParamIDs::b4Gain,
                              ParamIDs::b4Range, ParamIDs::b4Threshold, ParamIDs::b4Attack, ParamIDs::b4Release, ParamIDs::b4Listen,
                              ParamIDs::b4AutoRelease, ParamIDs::b4GainQ },
                            "Band 4", 1600.0f, false, {});

        addBandParameters (layout,
                            { ParamIDs::b5On, nullptr, ParamIDs::b5Freq, ParamIDs::b5Q, ParamIDs::b5Gain,
                              ParamIDs::b5Range, ParamIDs::b5Threshold, ParamIDs::b5Attack, ParamIDs::b5Release, ParamIDs::b5Listen,
                              ParamIDs::b5AutoRelease, ParamIDs::b5GainQ },
                            "Band 5", 4000.0f, false, {});

        addBandParameters (layout,
                            { ParamIDs::b6On, ParamIDs::b6Type, ParamIDs::b6Freq, ParamIDs::b6Q, ParamIDs::b6Gain,
                              ParamIDs::b6Range, ParamIDs::b6Threshold, ParamIDs::b6Attack, ParamIDs::b6Release, ParamIDs::b6Listen,
                              ParamIDs::b6AutoRelease, ParamIDs::b6GainQ },
                            "Band 6", 10000.0f, false, "High Shelf");

        //======================================================================
        // Global output trim, applied after Band 6 (and after the Mix
        // blend - see LancetEngine::process()).
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::outTrim, 1 },
            "Output Trim",
            juce::NormalisableRange<float> (-12.0f, 12.0f, 0.01f),
            0.0f,
            juce::AudioParameterFloatAttributes().withLabel ("dB")));

        // Mix: 0-100%, default 100% (fully processed/wet).
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::mix, 1 },
            "Mix",
            juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f),
            100.0f,
            juce::AudioParameterFloatAttributes().withLabel ("%")));

        return layout;
    }
}

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
        const char* sat;
    };

    // v0.3.0 (docs/voicing-notes.md): per-band default Q/Threshold/Attack/
    // Release, tuned to each band's typical role rather than the flat,
    // identical-across-all-bands v0.1/v0.2 defaults (Q 1.0, Threshold
    // -30 dB, Attack 5 ms, Release 150 ms everywhere). Range stays 0 dB for
    // every band regardless (a band must not move until the user engages it
    // - see docs/design-brief.md's own "zero-state is correct" reasoning,
    // unchanged here), so none of this is audible until a Range is dialed
    // in - but once it is, each band now starts from ballistics that suit
    // its documented role (low-frequency boom/resonance control moves
    // slowly; high-frequency harshness/sibilance control moves fast). See
    // docs/voicing-notes.md for full reasoning and the honesty section
    // (these are engineering judgment tuned to the existing per-band
    // frequency ladder, not sourced from a specific reference plugin
    // manual).
    struct BandVoicing
    {
        float q;
        float thresholdDb;
        float attackMs;
        float releaseMs;
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
                             const juce::String& shelfLabel,
                             const BandVoicing& voicing)
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

        // Q: 0.3 - 12, per-band default (v0.3.0, docs/voicing-notes.md).
        // Ignored in Shelf mode (fixed 0.707 - see DynamicBand.cpp).
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ids.q, 1 },
            labelPrefix + " Q",
            juce::NormalisableRange<float> (0.3f, 12.0f, 0.001f, 0.4f), // skewed toward the musically common low-Q end
            voicing.q));

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

        // Detector threshold: -60 - 0 dB, per-band default (v0.3.0,
        // docs/voicing-notes.md - was a flat -30 dB for every band).
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ids.threshold, 1 },
            labelPrefix + " Threshold",
            juce::NormalisableRange<float> (-60.0f, 0.0f, 0.01f),
            voicing.thresholdDb,
            juce::AudioParameterFloatAttributes().withLabel ("dB")));

        // Attack: 0.1 - 500 ms, per-band default (v0.3.0,
        // docs/voicing-notes.md - was a flat 5 ms for every band; v0.2.0
        // widened the range's own ends, floor 0.5 -> 0.1 ms, ceiling
        // 100 -> 500 ms, per docs/design-brief.md). Every per-band default
        // below still sits inside the v0.1/v0.2 range, so tolerant import
        // of a pre-v0.3.0 session needs no clamping.
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ids.attack, 1 },
            labelPrefix + " Attack",
            makeLogRange (0.1f, 500.0f),
            voicing.attackMs,
            juce::AudioParameterFloatAttributes().withLabel ("ms")));

        // Release: 5 - 1500 ms, per-band default (v0.3.0,
        // docs/voicing-notes.md - was a flat 150 ms for every band; v0.2.0
        // widened the range's own ends, floor 10 -> 5 ms, ceiling
        // 1000 -> 1500 ms, per docs/design-brief.md). Same subset property
        // as Attack above.
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ids.release, 1 },
            labelPrefix + " Release",
            makeLogRange (5.0f, 1500.0f),
            voicing.releaseMs,
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

        // Saturation (v0.3.0, docs/voicing-notes.md): off by default for
        // every band - an opt-in gentle waveshaper applied only while the
        // band's combined (static + dynamic) gain is actively boosting (see
        // DynamicBand::processSubBlock), never on a cut or an idle/off band.
        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { ids.sat, 1 }, labelPrefix + " Saturation", false));
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
        // defaults off, matching docs/design-brief.md. Per-band Q/
        // Threshold/Attack/Release voicing (v0.3.0, docs/voicing-notes.md)
        // tunes each band's ballistics to its documented role along the
        // existing frequency ladder - low-frequency boom/resonance control
        // (Band 1) moves slowly, high-frequency harshness/sibilance control
        // (Band 5) moves fast, Band 6's air-recovery shelf sits close to
        // Band 5. Range stays 0 dB (idle) for every band regardless.
        addBandParameters (layout,
                            { ParamIDs::b1On, ParamIDs::b1Type, ParamIDs::b1Freq, ParamIDs::b1Q, ParamIDs::b1Gain,
                              ParamIDs::b1Range, ParamIDs::b1Threshold, ParamIDs::b1Attack, ParamIDs::b1Release, ParamIDs::b1Listen,
                              ParamIDs::b1AutoRelease, ParamIDs::b1GainQ, ParamIDs::b1Sat },
                            "Band 1", 100.0f, false, "Low Shelf",
                            { 0.9f, -26.0f, 25.0f, 280.0f }); // boom/sub control: slow, gentle

        addBandParameters (layout,
                            { ParamIDs::b2On, nullptr, ParamIDs::b2Freq, ParamIDs::b2Q, ParamIDs::b2Gain,
                              ParamIDs::b2Range, ParamIDs::b2Threshold, ParamIDs::b2Attack, ParamIDs::b2Release, ParamIDs::b2Listen,
                              ParamIDs::b2AutoRelease, ParamIDs::b2GainQ, ParamIDs::b2Sat },
                            "Band 2", 250.0f, false, {},
                            { 1.1f, -28.0f, 15.0f, 180.0f }); // mud/box resonance: vocal & guitar body

        addBandParameters (layout,
                            { ParamIDs::b3On, nullptr, ParamIDs::b3Freq, ParamIDs::b3Q, ParamIDs::b3Gain,
                              ParamIDs::b3Range, ParamIDs::b3Threshold, ParamIDs::b3Attack, ParamIDs::b3Release, ParamIDs::b3Listen,
                              ParamIDs::b3AutoRelease, ParamIDs::b3GainQ, ParamIDs::b3Sat },
                            "Band 3", 630.0f, true, {},
                            { 1.0f, -26.0f, 8.0f, 130.0f }); // general midrange presence (default-on demo band)

        addBandParameters (layout,
                            { ParamIDs::b4On, nullptr, ParamIDs::b4Freq, ParamIDs::b4Q, ParamIDs::b4Gain,
                              ParamIDs::b4Range, ParamIDs::b4Threshold, ParamIDs::b4Attack, ParamIDs::b4Release, ParamIDs::b4Listen,
                              ParamIDs::b4AutoRelease, ParamIDs::b4GainQ, ParamIDs::b4Sat },
                            "Band 4", 1600.0f, false, {},
                            { 1.2f, -24.0f, 4.0f, 100.0f }); // vocal presence / guitar edge

        addBandParameters (layout,
                            { ParamIDs::b5On, nullptr, ParamIDs::b5Freq, ParamIDs::b5Q, ParamIDs::b5Gain,
                              ParamIDs::b5Range, ParamIDs::b5Threshold, ParamIDs::b5Attack, ParamIDs::b5Release, ParamIDs::b5Listen,
                              ParamIDs::b5AutoRelease, ParamIDs::b5GainQ, ParamIDs::b5Sat },
                            "Band 5", 4000.0f, false, {},
                            { 1.4f, -22.0f, 2.0f, 70.0f }); // sibilance / pick attack / harshness

        addBandParameters (layout,
                            { ParamIDs::b6On, ParamIDs::b6Type, ParamIDs::b6Freq, ParamIDs::b6Q, ParamIDs::b6Gain,
                              ParamIDs::b6Range, ParamIDs::b6Threshold, ParamIDs::b6Attack, ParamIDs::b6Release, ParamIDs::b6Listen,
                              ParamIDs::b6AutoRelease, ParamIDs::b6GainQ, ParamIDs::b6Sat },
                            "Band 6", 10000.0f, false, "High Shelf",
                            { 1.0f, -20.0f, 3.0f, 90.0f }); // air / fizz recovery shelf

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

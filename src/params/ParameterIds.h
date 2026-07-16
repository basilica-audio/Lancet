#pragma once

// Central definition of all AudioProcessorValueTreeState parameter IDs for
// Lancet. See docs/design-brief.md for the binding M1 parameter table and
// docs/architecture.md for the corresponding signal-flow diagram.
//
// FROZEN AS OF THE v0.1 PARAMETER LAYOUT:
// Parameter IDs below must NEVER change once shipped - saved sessions and
// presets persist the APVTS state keyed by these string IDs, and renaming or
// removing one would silently break every user's saved state. Ranges,
// defaults, and skew MAY still be refined during voicing/tuning milestones;
// only the IDs themselves are frozen.
//
// v0.2.0 (docs/design-brief.md) adds two new per-band IDs, `bN_autoRelease`
// and `bN_gainQ` - both new, both opt-in booleans defaulting to off, which is
// what makes a v0.1.0 AudioProcessorValueTreeState XML (missing these IDs
// entirely) a tolerant import: JUCE's own ValueTree/XML restore leaves any
// attribute absent from the loaded state at the ParameterLayout default, so
// no special-case migration code is needed (see docs/design-brief.md's
// "Versioning" section and tests/TolerantImportTests.cpp).
namespace ParamIDs
{
    // Per-band parameters, bands 1-6 in signal-flow order. Every band gets
    // On/Freq/Q/Gain/Range/Threshold/Attack/Release/Listen/AutoRelease/GainQ;
    // only Band 1 (LowShelf) and Band 6 (HighShelf) additionally get a Type
    // (Bell/Shelf) choice - see docs/design-brief.md's parameter table.
    inline constexpr auto b1On = "b1_on";
    inline constexpr auto b1Type = "b1_type";
    inline constexpr auto b1Freq = "b1_freq";
    inline constexpr auto b1Q = "b1_q";
    inline constexpr auto b1Gain = "b1_gain";
    inline constexpr auto b1Range = "b1_range";
    inline constexpr auto b1Threshold = "b1_thresh";
    inline constexpr auto b1Attack = "b1_attack";
    inline constexpr auto b1Release = "b1_release";
    inline constexpr auto b1Listen = "b1_listen";
    inline constexpr auto b1AutoRelease = "b1_autoRelease";
    inline constexpr auto b1GainQ = "b1_gainQ";

    inline constexpr auto b2On = "b2_on";
    inline constexpr auto b2Freq = "b2_freq";
    inline constexpr auto b2Q = "b2_q";
    inline constexpr auto b2Gain = "b2_gain";
    inline constexpr auto b2Range = "b2_range";
    inline constexpr auto b2Threshold = "b2_thresh";
    inline constexpr auto b2Attack = "b2_attack";
    inline constexpr auto b2Release = "b2_release";
    inline constexpr auto b2Listen = "b2_listen";
    inline constexpr auto b2AutoRelease = "b2_autoRelease";
    inline constexpr auto b2GainQ = "b2_gainQ";

    inline constexpr auto b3On = "b3_on";
    inline constexpr auto b3Freq = "b3_freq";
    inline constexpr auto b3Q = "b3_q";
    inline constexpr auto b3Gain = "b3_gain";
    inline constexpr auto b3Range = "b3_range";
    inline constexpr auto b3Threshold = "b3_thresh";
    inline constexpr auto b3Attack = "b3_attack";
    inline constexpr auto b3Release = "b3_release";
    inline constexpr auto b3Listen = "b3_listen";
    inline constexpr auto b3AutoRelease = "b3_autoRelease";
    inline constexpr auto b3GainQ = "b3_gainQ";

    inline constexpr auto b4On = "b4_on";
    inline constexpr auto b4Freq = "b4_freq";
    inline constexpr auto b4Q = "b4_q";
    inline constexpr auto b4Gain = "b4_gain";
    inline constexpr auto b4Range = "b4_range";
    inline constexpr auto b4Threshold = "b4_thresh";
    inline constexpr auto b4Attack = "b4_attack";
    inline constexpr auto b4Release = "b4_release";
    inline constexpr auto b4Listen = "b4_listen";
    inline constexpr auto b4AutoRelease = "b4_autoRelease";
    inline constexpr auto b4GainQ = "b4_gainQ";

    inline constexpr auto b5On = "b5_on";
    inline constexpr auto b5Freq = "b5_freq";
    inline constexpr auto b5Q = "b5_q";
    inline constexpr auto b5Gain = "b5_gain";
    inline constexpr auto b5Range = "b5_range";
    inline constexpr auto b5Threshold = "b5_thresh";
    inline constexpr auto b5Attack = "b5_attack";
    inline constexpr auto b5Release = "b5_release";
    inline constexpr auto b5Listen = "b5_listen";
    inline constexpr auto b5AutoRelease = "b5_autoRelease";
    inline constexpr auto b5GainQ = "b5_gainQ";

    inline constexpr auto b6On = "b6_on";
    inline constexpr auto b6Type = "b6_type";
    inline constexpr auto b6Freq = "b6_freq";
    inline constexpr auto b6Q = "b6_q";
    inline constexpr auto b6Gain = "b6_gain";
    inline constexpr auto b6Range = "b6_range";
    inline constexpr auto b6Threshold = "b6_thresh";
    inline constexpr auto b6Attack = "b6_attack";
    inline constexpr auto b6Release = "b6_release";
    inline constexpr auto b6Listen = "b6_listen";
    inline constexpr auto b6AutoRelease = "b6_autoRelease";
    inline constexpr auto b6GainQ = "b6_gainQ";

    // Global trim/mix, applied before Band 1 / after Band 6 respectively;
    // Mix is a parallel dry/wet blend around the whole six-band chain (see
    // LancetEngine::process()).
    inline constexpr auto inTrim = "in_trim";
    inline constexpr auto outTrim = "out_trim";
    inline constexpr auto mix = "mix";
}

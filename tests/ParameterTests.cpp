#include "PluginProcessor.h"
#include "params/ParameterIds.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

namespace
{
    juce::RangedAudioParameter* requireParam (juce::AudioProcessorValueTreeState& apvts, const juce::String& id)
    {
        auto* param = apvts.getParameter (id);
        REQUIRE (param != nullptr);
        return param;
    }

    void checkFloatRange (juce::AudioProcessorValueTreeState& apvts, const juce::String& id, float expectedMin, float expectedMax)
    {
        auto* param = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter (id));
        REQUIRE (param != nullptr);

        const auto range = param->getNormalisableRange().getRange();
        CHECK (range.getStart() == Catch::Approx (expectedMin));
        CHECK (range.getEnd() == Catch::Approx (expectedMax));
    }

    void checkFloatDefault (juce::AudioProcessorValueTreeState& apvts, const juce::String& id, float expectedDefault)
    {
        auto* param = requireParam (apvts, id);
        CHECK (param->getDefaultValue() == Catch::Approx (param->convertTo0to1 (expectedDefault)).margin (1e-4));
    }

    // Checks the six shared per-band parameters (Q/Gain/Range/Threshold/
    // Attack/Release) against docs/design-brief.md's common ranges/defaults
    // - Freq's default differs per band, so it is checked separately.
    void checkSharedBandDefaultsAndRanges (juce::AudioProcessorValueTreeState& apvts,
                                            const char* qId, const char* gainId, const char* rangeId,
                                            const char* thresholdId, const char* attackId, const char* releaseId)
    {
        checkFloatDefault (apvts, qId, 1.0f);
        checkFloatRange (apvts, qId, 0.3f, 12.0f);

        checkFloatDefault (apvts, gainId, 0.0f);
        checkFloatRange (apvts, gainId, -12.0f, 12.0f);

        checkFloatDefault (apvts, rangeId, 0.0f);
        checkFloatRange (apvts, rangeId, -12.0f, 12.0f);

        checkFloatDefault (apvts, thresholdId, -30.0f);
        checkFloatRange (apvts, thresholdId, -60.0f, 0.0f);

        checkFloatDefault (apvts, attackId, 5.0f);
        checkFloatRange (apvts, attackId, 0.1f, 500.0f);

        checkFloatDefault (apvts, releaseId, 150.0f);
        checkFloatRange (apvts, releaseId, 5.0f, 1500.0f);
    }

    // v0.2.0's two new per-band booleans (docs/design-brief.md §2/§3) - both
    // default off.
    void checkAutoReleaseAndGainQDefaultOff (juce::AudioProcessorValueTreeState& apvts,
                                              const char* autoReleaseId, const char* gainQId)
    {
        auto* autoRelease = dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter (autoReleaseId));
        REQUIRE (autoRelease != nullptr);
        CHECK (autoRelease->get() == false);

        auto* gainQ = dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter (gainQId));
        REQUIRE (gainQ != nullptr);
        CHECK (gainQ->get() == false);
    }
}

TEST_CASE ("Processor instantiates with the expected parameters", "[processor][parameters]")
{
    LancetAudioProcessor processor;
    auto& apvts = processor.apvts;

    SECTION ("plugin name")
    {
        CHECK (processor.getName() == juce::String ("Lancet"));
    }

    SECTION ("all documented parameter IDs resolve")
    {
        static constexpr const char* allIds[] = {
            ParamIDs::inTrim, ParamIDs::outTrim, ParamIDs::mix,

            ParamIDs::b1On, ParamIDs::b1Type, ParamIDs::b1Freq, ParamIDs::b1Q, ParamIDs::b1Gain, ParamIDs::b1Range,
            ParamIDs::b1Threshold, ParamIDs::b1Attack, ParamIDs::b1Release, ParamIDs::b1Listen,
            ParamIDs::b1AutoRelease, ParamIDs::b1GainQ,

            ParamIDs::b2On, ParamIDs::b2Freq, ParamIDs::b2Q, ParamIDs::b2Gain, ParamIDs::b2Range,
            ParamIDs::b2Threshold, ParamIDs::b2Attack, ParamIDs::b2Release, ParamIDs::b2Listen,
            ParamIDs::b2AutoRelease, ParamIDs::b2GainQ,

            ParamIDs::b3On, ParamIDs::b3Freq, ParamIDs::b3Q, ParamIDs::b3Gain, ParamIDs::b3Range,
            ParamIDs::b3Threshold, ParamIDs::b3Attack, ParamIDs::b3Release, ParamIDs::b3Listen,
            ParamIDs::b3AutoRelease, ParamIDs::b3GainQ,

            ParamIDs::b4On, ParamIDs::b4Freq, ParamIDs::b4Q, ParamIDs::b4Gain, ParamIDs::b4Range,
            ParamIDs::b4Threshold, ParamIDs::b4Attack, ParamIDs::b4Release, ParamIDs::b4Listen,
            ParamIDs::b4AutoRelease, ParamIDs::b4GainQ,

            ParamIDs::b5On, ParamIDs::b5Freq, ParamIDs::b5Q, ParamIDs::b5Gain, ParamIDs::b5Range,
            ParamIDs::b5Threshold, ParamIDs::b5Attack, ParamIDs::b5Release, ParamIDs::b5Listen,
            ParamIDs::b5AutoRelease, ParamIDs::b5GainQ,

            ParamIDs::b6On, ParamIDs::b6Type, ParamIDs::b6Freq, ParamIDs::b6Q, ParamIDs::b6Gain, ParamIDs::b6Range,
            ParamIDs::b6Threshold, ParamIDs::b6Attack, ParamIDs::b6Release, ParamIDs::b6Listen,
            ParamIDs::b6AutoRelease, ParamIDs::b6GainQ,
        };

        for (const auto* id : allIds)
            CHECK (apvts.getParameter (id) != nullptr);
    }

    SECTION ("total parameter count matches the v0.2.0 layout")
    {
        // 3 global (in/out trim, mix) + bands 1 & 6 (12 each: On, Type,
        // Freq, Q, Gain, Range, Threshold, Attack, Release, Listen,
        // AutoRelease, GainQ) + bands 2-5 (11 each: no Type) =
        // 3 + 24 + 44 = 71.
        CHECK (apvts.processor.getParameters().size() == 71);
    }

    SECTION ("AutoRelease/GainQ default off for every band")
    {
        checkAutoReleaseAndGainQDefaultOff (apvts, ParamIDs::b1AutoRelease, ParamIDs::b1GainQ);
        checkAutoReleaseAndGainQDefaultOff (apvts, ParamIDs::b2AutoRelease, ParamIDs::b2GainQ);
        checkAutoReleaseAndGainQDefaultOff (apvts, ParamIDs::b3AutoRelease, ParamIDs::b3GainQ);
        checkAutoReleaseAndGainQDefaultOff (apvts, ParamIDs::b4AutoRelease, ParamIDs::b4GainQ);
        checkAutoReleaseAndGainQDefaultOff (apvts, ParamIDs::b5AutoRelease, ParamIDs::b5GainQ);
        checkAutoReleaseAndGainQDefaultOff (apvts, ParamIDs::b6AutoRelease, ParamIDs::b6GainQ);
    }

    SECTION ("On defaults: every band off except Band 3")
    {
        for (const auto* id : { ParamIDs::b1On, ParamIDs::b2On, ParamIDs::b4On, ParamIDs::b5On, ParamIDs::b6On })
        {
            auto* param = dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter (id));
            REQUIRE (param != nullptr);
            CHECK (param->get() == false);
        }

        auto* band3On = dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter (ParamIDs::b3On));
        REQUIRE (band3On != nullptr);
        CHECK (band3On->get() == true);
    }

    SECTION ("Listen defaults off for every band")
    {
        for (const auto* id : { ParamIDs::b1Listen, ParamIDs::b2Listen, ParamIDs::b3Listen,
                                 ParamIDs::b4Listen, ParamIDs::b5Listen, ParamIDs::b6Listen })
        {
            auto* param = dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter (id));
            REQUIRE (param != nullptr);
            CHECK (param->get() == false);
        }
    }

    SECTION ("Type defaults to Bell for Band 1 and Band 6")
    {
        for (const auto* id : { ParamIDs::b1Type, ParamIDs::b6Type })
        {
            auto* param = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (id));
            REQUIRE (param != nullptr);
            CHECK (param->getIndex() == 0);
            CHECK (param->choices[0] == juce::String ("Bell"));
        }

        auto* b1Type = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (ParamIDs::b1Type));
        REQUIRE (b1Type != nullptr);
        CHECK (b1Type->choices[1] == juce::String ("Low Shelf"));

        auto* b6Type = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (ParamIDs::b6Type));
        REQUIRE (b6Type != nullptr);
        CHECK (b6Type->choices[1] == juce::String ("High Shelf"));
    }

    SECTION ("Frequency defaults: 100/250/630/1600/4000/10000 Hz, all 20 Hz - 20 kHz")
    {
        checkFloatDefault (apvts, ParamIDs::b1Freq, 100.0f);
        checkFloatDefault (apvts, ParamIDs::b2Freq, 250.0f);
        checkFloatDefault (apvts, ParamIDs::b3Freq, 630.0f);
        checkFloatDefault (apvts, ParamIDs::b4Freq, 1600.0f);
        checkFloatDefault (apvts, ParamIDs::b5Freq, 4000.0f);
        checkFloatDefault (apvts, ParamIDs::b6Freq, 10000.0f);

        for (const auto* id : { ParamIDs::b1Freq, ParamIDs::b2Freq, ParamIDs::b3Freq,
                                 ParamIDs::b4Freq, ParamIDs::b5Freq, ParamIDs::b6Freq })
            checkFloatRange (apvts, id, 20.0f, 20000.0f);
    }

    SECTION ("Band 1: shared defaults/ranges")
    {
        checkSharedBandDefaultsAndRanges (apvts, ParamIDs::b1Q, ParamIDs::b1Gain, ParamIDs::b1Range,
                                           ParamIDs::b1Threshold, ParamIDs::b1Attack, ParamIDs::b1Release);
    }

    SECTION ("Band 2: shared defaults/ranges")
    {
        checkSharedBandDefaultsAndRanges (apvts, ParamIDs::b2Q, ParamIDs::b2Gain, ParamIDs::b2Range,
                                           ParamIDs::b2Threshold, ParamIDs::b2Attack, ParamIDs::b2Release);
    }

    SECTION ("Band 3: shared defaults/ranges")
    {
        checkSharedBandDefaultsAndRanges (apvts, ParamIDs::b3Q, ParamIDs::b3Gain, ParamIDs::b3Range,
                                           ParamIDs::b3Threshold, ParamIDs::b3Attack, ParamIDs::b3Release);
    }

    SECTION ("Band 4: shared defaults/ranges")
    {
        checkSharedBandDefaultsAndRanges (apvts, ParamIDs::b4Q, ParamIDs::b4Gain, ParamIDs::b4Range,
                                           ParamIDs::b4Threshold, ParamIDs::b4Attack, ParamIDs::b4Release);
    }

    SECTION ("Band 5: shared defaults/ranges")
    {
        checkSharedBandDefaultsAndRanges (apvts, ParamIDs::b5Q, ParamIDs::b5Gain, ParamIDs::b5Range,
                                           ParamIDs::b5Threshold, ParamIDs::b5Attack, ParamIDs::b5Release);
    }

    SECTION ("Band 6: shared defaults/ranges")
    {
        checkSharedBandDefaultsAndRanges (apvts, ParamIDs::b6Q, ParamIDs::b6Gain, ParamIDs::b6Range,
                                           ParamIDs::b6Threshold, ParamIDs::b6Attack, ParamIDs::b6Release);
    }

    SECTION ("Global: In/Out Trim and Mix defaults/ranges")
    {
        checkFloatDefault (apvts, ParamIDs::inTrim, 0.0f);
        checkFloatRange (apvts, ParamIDs::inTrim, -12.0f, 12.0f);

        checkFloatDefault (apvts, ParamIDs::outTrim, 0.0f);
        checkFloatRange (apvts, ParamIDs::outTrim, -12.0f, 12.0f);

        checkFloatDefault (apvts, ParamIDs::mix, 100.0f);
        checkFloatRange (apvts, ParamIDs::mix, 0.0f, 100.0f);
    }
}

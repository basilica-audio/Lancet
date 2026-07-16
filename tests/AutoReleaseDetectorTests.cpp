#include "dsp/Detector.h"
#include "TestHelpers.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

// Detector-level companion to tests/AutoReleaseTests.cpp's engine-level
// guarantee #3 proof (docs/design-brief.md §4). Isolates the auto-release
// mechanism itself (Detector::setAutoRelease()'s fast reference envelope,
// see Detector.h's class docs) from DynamicBand's separate gain-computer/
// gain-smoothing layer, confirming directly at the envelope-follower level
// that enabling auto-release measurably speeds up the reported level's own
// decay for a naturally-decaying signal.
namespace
{
    constexpr double testSampleRate = 48000.0;
    constexpr double toneHz = 1000.0;
    constexpr int holdSamples = static_cast<int> (0.3 * testSampleRate);
    constexpr int tailSamples = static_cast<int> (1.2 * testSampleRate);
    constexpr float decayTauMs = 15.0f;
    constexpr float peakDbfs = -6.0f;
    constexpr float floorDbfs = -70.0f;

    // Builds a mono signal: `holdSamples` at a steady peak tone, followed by
    // `tailSamples` that either (isTransient) decay the tone's own
    // amplitude exponentially at `decayTauMs`, or step instantly to a flat,
    // near-silent floor and stay there.
    juce::AudioBuffer<float> buildSignal (bool isTransient)
    {
        juce::AudioBuffer<float> buffer (1, holdSamples + tailSamples);
        auto* data = buffer.getWritePointer (0);

        const auto peakAmplitude = 0.5f * juce::Decibels::decibelsToGain (peakDbfs);
        const auto floorAmplitude = 0.5f * juce::Decibels::decibelsToGain (floorDbfs);
        const auto tauSamples = static_cast<double> (decayTauMs) * 0.001 * testSampleRate;

        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            float amplitude;

            if (i < holdSamples)
            {
                amplitude = peakAmplitude;
            }
            else if (isTransient)
            {
                const auto tailIndex = static_cast<double> (i - holdSamples);
                amplitude = peakAmplitude * static_cast<float> (std::exp (-tailIndex / tauSamples));
                amplitude = juce::jmax (amplitude, floorAmplitude);
            }
            else
            {
                amplitude = floorAmplitude;
            }

            const auto phase = juce::MathConstants<double>::twoPi * toneHz * static_cast<double> (i) / testSampleRate;
            data[i] = amplitude * static_cast<float> (std::sin (phase));
        }

        return buffer;
    }

    // Processes `signal` through a Detector configured with a deliberately
    // slow (500 ms) manual Release and returns the reported level (dBFS)
    // 50 ms after the tail begins - long enough to show a clear speed-up if
    // auto-release is engaging, short enough that the plain manual case is
    // still nowhere near settled.
    float measureLevelDbAtTailPlus50ms (const juce::AudioBuffer<float>& signal, bool autoRelease)
    {
        Detector detector;
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = testSampleRate;
        spec.maximumBlockSize = static_cast<juce::uint32> (signal.getNumSamples());
        spec.numChannels = 1;
        detector.prepare (spec);

        detector.setFrequencyAndQ (static_cast<float> (toneHz), 1.0f);
        detector.setAttackMs (2.0f);
        detector.setReleaseMs (500.0f);
        detector.setAutoRelease (autoRelease);

        const juce::dsp::AudioBlock<const float> block (signal);

        constexpr size_t subBlockSize = 32;
        const auto targetSample = static_cast<size_t> (holdSamples + static_cast<int> (0.05 * testSampleRate));

        size_t position = 0;
        float level = 0.0f;

        while (position < static_cast<size_t> (signal.getNumSamples()))
        {
            const auto n = juce::jmin (subBlockSize, static_cast<size_t> (signal.getNumSamples()) - position);
            level = detector.processSubBlock (block, position, n);
            position += n;

            if (position >= targetSample)
                break;
        }

        return level;
    }
}

TEST_CASE ("Detector auto-release: a naturally-decaying transient's reported level drops measurably "
           "faster with auto-release on than with the plain manual Release",
           "[dsp][detector][auto-release]")
{
    const auto transientSignal = buildSignal (true);

    const auto transientManual = measureLevelDbAtTailPlus50ms (transientSignal, false);
    const auto transientAuto = measureLevelDbAtTailPlus50ms (transientSignal, true);

    INFO ("transientManual=" << transientManual << " transientAuto=" << transientAuto);

    // Auto-release must report a measurably LOWER (further decayed) level
    // than manual at this fixed 50 ms snapshot, for the naturally-decaying
    // transient - direct proof the fast reference envelope is actually
    // driving a faster effective release, not just a wired-but-inert flag.
    CHECK (transientAuto < transientManual - 3.0f);
}

TEST_CASE ("Detector auto-release: an abrupt, flat-lined drop settles no slower than the plain manual Release",
           "[dsp][detector][auto-release]")
{
    const auto sustainedSignal = buildSignal (false);

    const auto sustainedManual = measureLevelDbAtTailPlus50ms (sustainedSignal, false);
    const auto sustainedAuto = measureLevelDbAtTailPlus50ms (sustainedSignal, true);

    INFO ("sustainedManual=" << sustainedManual << " sustainedAuto=" << sustainedAuto);

    // "Always <= the Release setting" (F6 ARC's documented promise) - never
    // slower than manual, even for a signal shape auto-release isn't
    // specifically optimised for.
    CHECK (sustainedAuto <= sustainedManual + 0.5f);
}

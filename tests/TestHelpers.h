#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <cmath>

// Small shared helpers used across the Tests target. Kept dependency-free
// (just juce_audio_basics) so it can be included from any test file.
namespace TestHelpers
{
    // Fills every channel of the buffer with a sine wave of the given
    // frequency. `startSampleIndex` offsets the phase calculation, so
    // calling this for consecutive blocks with startSampleIndex incremented
    // by each block's length produces a phase-continuous sine across block
    // boundaries (needed whenever a test processes multiple blocks through a
    // stateful IIR/envelope-follower processor - a phase discontinuity at
    // block boundaries would inject spurious broadband energy and pollute
    // level measurements).
    inline void fillWithSine (juce::AudioBuffer<float>& buffer,
                              double sampleRate,
                              double frequencyHz,
                              float amplitude = 0.5f,
                              juce::int64 startSampleIndex = 0)
    {
        const auto numChannels = buffer.getNumChannels();
        const auto numSamples = buffer.getNumSamples();

        for (int channel = 0; channel < numChannels; ++channel)
        {
            auto* data = buffer.getWritePointer (channel);

            for (int sample = 0; sample < numSamples; ++sample)
            {
                const auto phase = juce::MathConstants<double>::twoPi * frequencyHz
                                    * static_cast<double> (startSampleIndex + sample) / sampleRate;
                data[sample] = amplitude * static_cast<float> (std::sin (phase));
            }
        }
    }

    // Root-mean-square level across all channels/samples in the buffer.
    inline double rms (const juce::AudioBuffer<float>& buffer)
    {
        double sumOfSquares = 0.0;
        juce::int64 numValues = 0;

        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            const auto* data = buffer.getReadPointer (channel);

            for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            {
                const auto value = static_cast<double> (data[sample]);
                sumOfSquares += value * value;
                ++numValues;
            }
        }

        return numValues > 0 ? std::sqrt (sumOfSquares / static_cast<double> (numValues)) : 0.0;
    }

    // RMS level of a single channel over [startSample, buffer.getNumSamples()).
    inline double tailRms (const juce::AudioBuffer<float>& buffer, int startSample, int channel = 0)
    {
        double sumOfSquares = 0.0;
        int counted = 0;

        const auto* data = buffer.getReadPointer (channel);

        for (int i = startSample; i < buffer.getNumSamples(); ++i)
        {
            sumOfSquares += static_cast<double> (data[i]) * static_cast<double> (data[i]);
            ++counted;
        }

        return counted > 0 ? std::sqrt (sumOfSquares / static_cast<double> (counted)) : 0.0;
    }

    // Largest absolute sample value across all channels/samples.
    inline float peakAbsolute (const juce::AudioBuffer<float>& buffer)
    {
        float peak = 0.0f;

        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            const auto* data = buffer.getReadPointer (channel);

            for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
                peak = std::max (peak, std::abs (data[sample]));
        }

        return peak;
    }

    // Returns true if every sample in the buffer is finite (no NaN/Inf).
    inline bool allSamplesFinite (const juce::AudioBuffer<float>& buffer)
    {
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            const auto* data = buffer.getReadPointer (channel);

            for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
                if (! std::isfinite (data[sample]))
                    return false;
        }

        return true;
    }

    // Largest absolute sample-to-sample difference across all channels -
    // used by the zipper-guard test to bound automation smoothness.
    inline float maxSampleToSampleJump (const juce::AudioBuffer<float>& buffer)
    {
        float maxJump = 0.0f;

        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            const auto* data = buffer.getReadPointer (channel);

            for (int sample = 1; sample < buffer.getNumSamples(); ++sample)
                maxJump = std::max (maxJump, std::abs (data[sample] - data[sample - 1]));
        }

        return maxJump;
    }

    // Analytic RBJ "Audio EQ Cookbook" peaking-EQ magnitude response in dB,
    // computed independently of juce::dsp::IIR::ArrayCoefficients (which
    // implements the same cookbook formula) - used as the reference for
    // docs/design-brief.md guarantee #2 ("static correctness... within
    // +-0.5 dB of the analytic RBJ response").
    inline double rbjPeakMagnitudeDb (double sampleRate, double centreFrequencyHz, double q, double gainDb, double probeFrequencyHz)
    {
        const auto a = std::pow (10.0, gainDb / 40.0);
        const auto w0 = 2.0 * juce::MathConstants<double>::pi * centreFrequencyHz / sampleRate;
        const auto alpha = std::sin (w0) / (2.0 * q);
        const auto cosW0 = std::cos (w0);

        const auto b0 = 1.0 + alpha * a;
        const auto b1 = -2.0 * cosW0;
        const auto b2 = 1.0 - alpha * a;
        const auto a0 = 1.0 + alpha / a;
        const auto a1 = -2.0 * cosW0;
        const auto a2 = 1.0 - alpha / a;

        const auto w = 2.0 * juce::MathConstants<double>::pi * probeFrequencyHz / sampleRate;
        const std::complex<double> z = std::polar (1.0, w);
        const auto num = b0 + b1 / z + b2 / (z * z);
        const auto den = a0 + a1 / z + a2 / (z * z);

        return 20.0 * std::log10 (std::abs (num / den));
    }

    // Analytic RBJ low/high-shelf magnitude response in dB. `isLowShelf`
    // selects the low-shelf (true) or high-shelf (false) cookbook formula.
    inline double rbjShelfMagnitudeDb (double sampleRate, double cornerFrequencyHz, double q, double gainDb, double probeFrequencyHz, bool isLowShelf)
    {
        const auto a = std::pow (10.0, gainDb / 40.0);
        const auto w0 = 2.0 * juce::MathConstants<double>::pi * cornerFrequencyHz / sampleRate;
        const auto alpha = std::sin (w0) / (2.0 * q);
        const auto cosW0 = std::cos (w0);
        const auto sqrtA = std::sqrt (a);

        double b0, b1, b2, a0, a1, a2;

        if (isLowShelf)
        {
            b0 = a * ((a + 1.0) - (a - 1.0) * cosW0 + 2.0 * sqrtA * alpha);
            b1 = 2.0 * a * ((a - 1.0) - (a + 1.0) * cosW0);
            b2 = a * ((a + 1.0) - (a - 1.0) * cosW0 - 2.0 * sqrtA * alpha);
            a0 = (a + 1.0) + (a - 1.0) * cosW0 + 2.0 * sqrtA * alpha;
            a1 = -2.0 * ((a - 1.0) + (a + 1.0) * cosW0);
            a2 = (a + 1.0) + (a - 1.0) * cosW0 - 2.0 * sqrtA * alpha;
        }
        else
        {
            b0 = a * ((a + 1.0) + (a - 1.0) * cosW0 + 2.0 * sqrtA * alpha);
            b1 = -2.0 * a * ((a - 1.0) + (a + 1.0) * cosW0);
            b2 = a * ((a + 1.0) + (a - 1.0) * cosW0 - 2.0 * sqrtA * alpha);
            a0 = (a + 1.0) - (a - 1.0) * cosW0 + 2.0 * sqrtA * alpha;
            a1 = 2.0 * ((a - 1.0) - (a + 1.0) * cosW0);
            a2 = (a + 1.0) - (a - 1.0) * cosW0 - 2.0 * sqrtA * alpha;
        }

        const auto w = 2.0 * juce::MathConstants<double>::pi * probeFrequencyHz / sampleRate;
        const std::complex<double> z = std::polar (1.0, w);
        const auto num = b0 + b1 / z + b2 / (z * z);
        const auto den = a0 + a1 / z + a2 / (z * z);

        return 20.0 * std::log10 (std::abs (num / den));
    }
}

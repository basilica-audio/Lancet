#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

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

    //==========================================================================
    // Spectral measurement helpers (v0.4.0). Added for the SOTA brief's
    // measurable-DSP test plan: the aliasing, zipper-sideband and static-null
    // assertions all need real spectral numbers rather than RMS ratios.

    // Complex amplitude of `frequencyHz` in one channel over
    // [startSample, startSample + numSamples), by direct correlation with a
    // unit sin/cos pair. Exact (no leakage, no window needed) whenever the
    // measured range covers a whole number of periods of `frequencyHz`,
    // which every caller here arranges deliberately; that is the whole
    // reason for using this rather than a windowed FFT bin.
    inline std::complex<double> toneAmplitude (const juce::AudioBuffer<float>& buffer,
                                                int channel,
                                                int startSample,
                                                int numSamples,
                                                double frequencyHz,
                                                double sampleRate)
    {
        const auto* data = buffer.getReadPointer (channel);

        double real = 0.0;
        double imaginary = 0.0;

        for (int i = 0; i < numSamples; ++i)
        {
            const auto phase = juce::MathConstants<double>::twoPi * frequencyHz
                                * static_cast<double> (startSample + i) / sampleRate;
            const auto value = static_cast<double> (data[startSample + i]);

            real += value * std::cos (phase);
            imaginary -= value * std::sin (phase);
        }

        const auto scale = 2.0 / static_cast<double> (numSamples);
        return { real * scale, imaginary * scale };
    }

    // Mean square (i.e. power) of one channel over a range.
    inline double meanSquare (const juce::AudioBuffer<float>& buffer, int channel, int startSample, int numSamples)
    {
        const auto* data = buffer.getReadPointer (channel);

        double sum = 0.0;

        for (int i = 0; i < numSamples; ++i)
            sum += static_cast<double> (data[startSample + i]) * static_cast<double> (data[startSample + i]);

        return numSamples > 0 ? sum / static_cast<double> (numSamples) : 0.0;
    }

    // Power carried by a pure tone of the given complex amplitude.
    inline double tonePower (const std::complex<double>& amplitude)
    {
        return 0.5 * std::norm (amplitude);
    }

    // Hann-windowed magnitude spectrum (linear, one-sided) of one channel,
    // `fftOrder` samples starting at `startSample`. Used where a whole
    // spectrum is needed rather than a handful of known frequencies - e.g.
    // deciding whether a discrete line stands above a smooth skirt.
    inline std::vector<float> magnitudeSpectrum (const juce::AudioBuffer<float>& buffer,
                                                  int channel,
                                                  int startSample,
                                                  int fftOrder)
    {
        const auto fftSize = 1 << fftOrder;
        jassert (startSample + fftSize <= buffer.getNumSamples());

        juce::dsp::FFT fft (fftOrder);
        juce::dsp::WindowingFunction<float> window (static_cast<size_t> (fftSize),
                                                     juce::dsp::WindowingFunction<float>::hann);

        std::vector<float> scratch (static_cast<size_t> (fftSize) * 2, 0.0f);
        const auto* data = buffer.getReadPointer (channel);

        for (int i = 0; i < fftSize; ++i)
            scratch[static_cast<size_t> (i)] = data[startSample + i];

        window.multiplyWithWindowingTable (scratch.data(), static_cast<size_t> (fftSize));
        fft.performFrequencyOnlyForwardTransform (scratch.data());

        scratch.resize (static_cast<size_t> (fftSize / 2));
        return scratch;
    }

    // Largest magnitude over [firstBin, lastBin], skipping any bin within
    // `excludeRadius` of `excludeBin`.
    //
    // This is the reference a suspected artefact line has to be compared
    // against, and it is deliberately a peak rather than a mean or median:
    // the surrounding spectrum is generally not a smooth floor but a comb
    // (an amplitude-modulated carrier's sidebands sit on discrete lines with
    // gaps between them), so a median would sample the gaps and make every
    // ordinary sideband look like an artefact standing above it. Comparing a
    // line against the peaks of its neighbours asks the right question: does
    // *this* line stand out from the structure it sits in?
    inline double peakMagnitudeExcluding (const std::vector<float>& spectrum,
                                           int firstBin,
                                           int lastBin,
                                           int excludeBin,
                                           int excludeRadius)
    {
        double peak = 0.0;

        for (int bin = firstBin; bin <= lastBin; ++bin)
        {
            if (bin < 0 || bin >= static_cast<int> (spectrum.size()))
                continue;

            if (std::abs (bin - excludeBin) <= excludeRadius)
                continue;

            peak = std::max (peak, static_cast<double> (spectrum[static_cast<size_t> (bin)]));
        }

        return peak;
    }

    // Root-mean-square of the sample-to-sample differences at the sample
    // positions selected by `selectPosition`. Comparing the value at
    // sub-block boundaries with the value everywhere else is a direct test
    // for a stepped (per-sub-block) gain path: a stepped implementation
    // concentrates its discontinuities on the boundaries, a per-sample one
    // spreads them evenly.
    template <typename PositionPredicate>
    double jumpRms (const juce::AudioBuffer<float>& buffer, int channel, int startSample, PositionPredicate selectPosition)
    {
        const auto* data = buffer.getReadPointer (channel);

        double sum = 0.0;
        int counted = 0;

        for (int i = juce::jmax (1, startSample); i < buffer.getNumSamples(); ++i)
        {
            if (! selectPosition (i))
                continue;

            const auto difference = static_cast<double> (data[i]) - static_cast<double> (data[i - 1]);
            sum += difference * difference;
            ++counted;
        }

        return counted > 0 ? std::sqrt (sum / static_cast<double> (counted)) : 0.0;
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

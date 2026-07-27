#pragma once

#include <juce_core/juce_core.h>

#include <cmath>
#include <vector>

// First-order antiderivative anti-aliasing (ADAA1) for the tanh waveshaper
// Lancet's per-band Saturation stage uses.
//
// Background (v0.4.0, SOTA brief F5): evaluating a memoryless nonlinearity
// sample by sample creates harmonics far above Nyquist which fold straight
// back into the audible band. The classic fix is oversampling, which costs
// latency (linear-phase) or phase distortion (minimum-phase) plus a lot of
// CPU. ADAA instead integrates the nonlinearity analytically over the
// segment between consecutive samples:
//
//     y[n] = ( F0(x[n]) - F0(x[n-1]) ) / ( x[n] - x[n-1] )      with F0' = f
//
// which is the *average* of f along the linear path between the two
// samples. That average is equivalent to applying a one-sample boxcar to
// the continuous-time distortion product before sampling it, i.e. the
// harmonic at frequency f is attenuated by |sinc(f/fs)| before it folds.
// For Lancet's worst realistic case - a 10 kHz tone at 48 kHz, whose
// dominant fold is the 3rd harmonic at 30 kHz landing on 18 kHz - that is
// sin(pi*0.625)/(pi*0.625) ~= 0.47, about 6.5 dB of suppression, and much
// more for every higher harmonic (the 5th, at 50 kHz, is suppressed ~28 dB).
// Lineage: Parker/Zavalishin/Le Bivic, "Reducing the aliasing of nonlinear
// waveshaping using continuous-time convolution" (DAFx-16); Bilbao et al.
// on antiderivative antialiasing. Implemented from the published equations;
// no third-party code is vendored.
//
// Honesty (SOTA brief F5 / Risk 5): base-rate ADAA1 is chosen here because
// this saturator is gentle (drive <= 2.5) and must stay at zero latency,
// but our research only ever *measured* an absolute alias floor for
// 2x-oversampled ADAA1. The claim this implementation makes is therefore a
// relative one - measurably less aliasing than the memoryless tanh it
// replaces at pinned input levels - and that is exactly what
// tests/AliasingTests.cpp asserts. No absolute alias-floor number is
// claimed anywhere.
//
// The antiderivative of tanh is ln(cosh(x)), which overflows for |x| > ~700
// if evaluated naively. The identity
//
//     ln(cosh(x)) = |x| + log1p(exp(-2|x|)) - ln(2)
//
// is exact and overflow-free for every finite x.
//
// The difference quotient is ill-conditioned when the two samples are very
// close together (0/0). Below a small threshold the kernel falls back to
// the midpoint value tanh((x + x1)/2), which is the limit of the quotient
// and matches it to second order, so the crossover is inaudible.
//
// State: ONE kernel per channel, never shared. Lancet processes a band's
// channels in separate passes, so a single shared previous-sample value
// would make each channel's first difference quotient span the *other*
// channel's last sample - broadband garbage rather than saturation. Pinned
// by tests/AliasingTests.cpp's stereo state-independence case.
namespace lnct
{
    class AdaaTanh
    {
    public:
        // Overflow-safe first antiderivative of tanh: ln(cosh(x)).
        static double antiderivative (double x) noexcept
        {
            constexpr double lnTwo = 0.6931471805599453;
            const auto absX = std::abs (x);
            return absX + std::log1p (std::exp (-2.0 * absX)) - lnTwo;
        }

        // Allocates one previous-sample slot per channel. Must be called
        // before the first process() call and whenever the channel count
        // changes.
        void prepare (int numChannels)
        {
            previousInput.assign (static_cast<size_t> (juce::jmax (0, numChannels)), 0.0f);
        }

        // Clears state without deallocating. Real-time safe.
        void reset() noexcept
        {
            for (auto& value : previousInput)
                value = 0.0f;
        }

        int getNumChannels() const noexcept { return static_cast<int> (previousInput.size()); }

        // Keeps a channel's previous-sample state current without applying
        // any distortion. Called for every sample the owning band processes,
        // including while the saturation stage itself is bypassed (cutting
        // or idle band), so that engaging saturation mid-stream never starts
        // from a stale difference quotient.
        void pushBypassed (size_t channel, float input) noexcept
        {
            previousInput[channel] = input;
        }

        // ADAA1-shaped tanh(x*drive)/drive for one sample of one channel.
        // `drive` must be > 0. Unity slope at the origin regardless of
        // drive, exactly like the memoryless kernel it replaces.
        //
        // The previous-sample state is stored *undriven* rather than
        // pre-multiplied by the drive: the drive itself is now derived from
        // the band's per-sample applied gain and therefore moves, and
        // rescaling a stale driven value would inject a spurious step into
        // the difference quotient whenever it did. For a constant drive the
        // two formulations are identical.
        float process (size_t channel, float input, float drive) noexcept
        {
            const auto d = static_cast<double> (drive);
            const auto x0 = static_cast<double> (input) * d;
            const auto x1 = static_cast<double> (previousInput[channel]) * d;

            previousInput[channel] = input;

            const auto delta = x0 - x1;

            const auto y = std::abs (delta) > differenceQuotientFloor
                             ? (antiderivative (x0) - antiderivative (x1)) / delta
                             : std::tanh (0.5 * (x0 + x1));

            if (! std::isfinite (y))
            {
                previousInput[channel] = 0.0f;
                return 0.0f;
            }

            return static_cast<float> (y / d);
        }

    private:
        static constexpr double differenceQuotientFloor = 1.0e-4;

        std::vector<float> previousInput;
    };
}

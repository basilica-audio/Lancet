#pragma once

#include <juce_dsp/juce_dsp.h>

#include <array>
#include <cmath>
#include <vector>

// Topology-Preserving-Transform (trapezoidal-integration) state-variable
// filter, in the bell / low-shelf / high-shelf forms Lancet's bands need.
//
// Why this exists (v0.4.0, see the SOTA brief's F1/F2): Lancet up to v0.3.0
// ran each band through an RBJ biquad whose coefficients were recomputed
// once per 32-sample sub-block from a 50 ms-smoothed gain value. That
// smoother - not the user's Attack knob - was the real ballistics
// bottleneck: no dialed Attack below ~50 ms could ever be heard, because the
// gain that the filter actually realised was itself low-passed at 50 ms.
// Removing the smoother from a *coefficient-recomputing* biquad is not an
// option: a direct-form biquad whose {b,a} coefficients jump every sample is
// not a well-defined time-varying filter (its internal state means something
// different after each jump), which is exactly what produces zipper noise.
//
// A TPT SVF does not have that problem. Its state variables are the two
// integrator outputs, which keep their physical meaning (band-pass and
// low-pass integrator states) no matter how the coefficients move, so the
// gain can be modulated *per sample* by a continuous control signal - the
// detector envelope - without any zipper artefact. That is what makes the
// per-sample dynamic gain path of v0.4.0 possible at all.
//
// Discretisation: Andrew Simper (Cytomic), "Solving the continuous SVF
// equations using trapezoidal integration and equivalent currents"
// (cytomic.com technical paper). Implemented here from the published
// equations - no third-party code is vendored.
//
// The realised transfer functions are *identical* to the corresponding RBJ
// "Audio EQ Cookbook" biquads at the same (f0, Q, gain), which is what lets
// v0.4.0 swap the band core in without changing any static EQ curve. Sketch
// for the bell, with s normalised to w0 and the SVF outputs
// LP = 1/(s^2+ks+1), BP = s/(s^2+ks+1):
//
//   y = x + m1*BP  =>  H(s) = (s^2 + (k+m1)s + 1) / (s^2 + ks + 1)
//   k  = 1/(Q*A), m1 = k*(A^2-1)  =>  k + m1 = A/Q
//   =>  H(s) = (s^2 + (A/Q)s + 1) / (s^2 + (1/(A*Q))s + 1)
//
// which is exactly RBJ's peaking-EQ analogue prototype; the trapezoidal
// (bilinear) map with the pre-warp g = tan(pi*f0/fs) is the same bilinear
// map RBJ's alpha = sin(w0)/(2Q) encodes, since g/(1+g^2) == sin(w0)/2. The
// same argument holds for the two shelf forms (their g is additionally
// scaled by 1/sqrt(A) resp. sqrt(A), which is precisely the frequency
// scaling in RBJ's shelf prototypes). tests/StaticResponseTests.cpp and
// tests/NullTests.cpp pin this equivalence numerically.
//
// Numerical precision: the integrator state and the per-sample update are
// kept in `double` even though the audio interface is `float`. The
// trapezoidal update contains the deliberate cancellation
// `ic1 = 2*v1 - ic1`, which at low frequencies (g ~ 1e-3 at 20 Hz/48 kHz)
// and high Q loses several digits in single precision - and this filter is
// modulated per sample, so the error does not average out. Double state
// removes that entire risk class for ~free on any modern CPU, and it is
// what lets the -100 dBFS null bound of tests/NullTests.cpp hold. See also
// the SOTA brief's Risk 4.
//
// Free-standing on purpose: this header knows nothing about DynamicBand or
// Detector, so the planned v0.5.0 spectral-suppression module can reuse it
// as its filter-bank primitive without dragging the dynamics engine along.
namespace lnct
{
    class TptSvf
    {
    public:
        enum class Type
        {
            bell,
            lowShelf,
            highShelf
        };

        // One sample's worth of filter scalars: the two integrator
        // coefficients derived from g and the damping k, plus the output
        // mix. Deliberately a value type - the per-sample gain path
        // computes one of these per sample and applies it to every channel,
        // so the (comparatively expensive) exp/div work happens once per
        // sample rather than once per sample *per channel*.
        struct Coefficients
        {
            double a1 = 1.0;
            double a2 = 0.0;
            double a3 = 0.0;
            double m0 = 1.0;
            double m1 = 0.0;
            double m2 = 0.0;
        };

        // NaN-safety clamps (house rule, SOTA brief §3): every quantity that
        // feeds a division or an exponential is bounded before use, so no
        // combination of automation, denormal input or extreme sample rate
        // can produce a non-finite coefficient.
        static constexpr float maxTotalGainDb = 24.0f;
        static constexpr double minG = 1.0e-6;
        static constexpr double maxG = 30.0;
        static constexpr double minDamping = 1.0e-4;
        static constexpr double maxDamping = 1.0e4;

        // Shelves use the standard "flat"/Butterworth slope, matching what
        // juce::dsp::IIR::ArrayCoefficients::makeLowShelf/makeHighShelf
        // realise for Q = 1/sqrt(2) - the value Lancet's shelf bands have
        // always used.
        static constexpr float defaultShelfQ = 0.70710678f;

        // The un-gained frequency warp for a centre/corner frequency, i.e.
        // tan(pi*f0/fs). Recomputed only when frequency or sample rate
        // actually change (see DynamicBand's dirty flagging) - this is the
        // only trigonometric call in the whole band path.
        static double computeGBase (double frequencyHz, double sampleRate) noexcept
        {
            const auto nyquist = sampleRate * 0.5;
            const auto clampedFrequency = juce::jlimit (1.0, nyquist * 0.999, frequencyHz);
            const auto g = std::tan (juce::MathConstants<double>::pi * clampedFrequency / sampleRate);
            return juce::jlimit (minG, maxG, g);
        }

        // Linear gain "A" in the RBJ sense: A = 10^(dB/40), so A^2 is the
        // filter's actual peak/plateau gain. Computed with exp() rather
        // than pow() - this runs once per sample per band.
        static double computeA (float totalGainDb) noexcept
        {
            const auto clampedDb = juce::jlimit (-maxTotalGainDb, maxTotalGainDb, totalGainDb);

            if (clampedDb == 0.0f)
                return 1.0; // exact, so an idle band is bit-transparent (see makeCoefficients)

            constexpr double lnTenOverForty = 2.302585092994046 / 40.0;
            return std::exp (static_cast<double> (clampedDb) * lnTenOverForty);
        }

        // Builds the per-sample scalars for one band type.
        //
        // `gBase` is computeGBase() for the band's centre/corner frequency;
        // `qEff` is the band's effective Q (shelves pass defaultShelfQ);
        // `a` is computeA(totalGainDb).
        //
        // At a == 1 every gain-dependent mix term is *exactly* zero
        // (bell: m1 = k*(A^2-1); low shelf: m1 = k*(A-1), m2 = A^2-1;
        // high shelf: m0 = A^2 = 1, m1 = k*(1-A)*A, m2 = 1-A^2), so
        // processSample() returns its input bit-for-bit. That is what keeps
        // the design brief's "band on at 0 dB is bit-transparent" guarantee
        // true without any special-case branch inside the sample loop.
        static Coefficients makeCoefficients (Type type, double gBase, float qEff, double a) noexcept
        {
            Coefficients c;

            const auto q = juce::jlimit (0.01, 100.0, static_cast<double> (qEff));

            double g = gBase;
            double k = 0.0;

            switch (type)
            {
                case Type::bell:
                {
                    // Constant-Q symmetric boost/cut: damping scales with
                    // 1/A so that a +N dB boost and a -N dB cut are exact
                    // mirror images (the RBJ peaking convention).
                    k = 1.0 / (q * a);
                    c.m0 = 1.0;
                    c.m1 = k * (a * a - 1.0);
                    c.m2 = 0.0;
                    break;
                }

                case Type::lowShelf:
                {
                    const auto sqrtA = std::sqrt (a);
                    g = gBase / sqrtA;
                    k = 1.0 / q;
                    c.m0 = 1.0;
                    c.m1 = k * (a - 1.0);
                    c.m2 = a * a - 1.0;
                    break;
                }

                case Type::highShelf:
                {
                    const auto sqrtA = std::sqrt (a);
                    g = gBase * sqrtA;
                    k = 1.0 / q;
                    c.m0 = a * a;
                    c.m1 = k * (1.0 - a) * a;
                    c.m2 = 1.0 - a * a;
                    break;
                }
            }

            g = juce::jlimit (minG, maxG, g);
            k = juce::jlimit (minDamping, maxDamping, k);

            c.a1 = 1.0 / (1.0 + g * (g + k));
            c.a2 = g * c.a1;
            c.a3 = g * c.a2;

            return c;
        }

        // Allocates per-channel integrator state. Must be called before the
        // first processSample() call and whenever the channel count changes.
        void prepare (int numChannels)
        {
            state.assign (static_cast<size_t> (juce::jmax (0, numChannels)), ChannelState {});
        }

        // Clears integrator state without deallocating. Real-time safe.
        void reset() noexcept
        {
            for (auto& s : state)
                s = ChannelState {};
        }

        int getNumChannels() const noexcept { return static_cast<int> (state.size()); }

        // One trapezoidal update step for one channel. `channel` must be
        // < getNumChannels(). Real-time safe, no allocation, no branches on
        // the audio data.
        float processSample (size_t channel, float input, const Coefficients& c) noexcept
        {
            auto& s = state[channel];

            const auto x = static_cast<double> (input);

            const auto v3 = x - s.ic2;
            const auto v1 = c.a1 * s.ic1 + c.a2 * v3;
            const auto v2 = s.ic2 + c.a2 * s.ic1 + c.a3 * v3;

            s.ic1 = 2.0 * v1 - s.ic1;
            s.ic2 = 2.0 * v2 - s.ic2;

            const auto y = c.m0 * x + c.m1 * v1 + c.m2 * v2;

            // A non-finite input (host glitch, upstream NaN) would otherwise
            // poison the integrator state permanently - the recursion has no
            // way back out of NaN on its own. Snapping the state clean here
            // is what lets tests/RobustnessTests.cpp's NaN/Inf sweeps
            // recover within the same block rather than needing a reset().
            if (! std::isfinite (y) || ! std::isfinite (s.ic1) || ! std::isfinite (s.ic2))
            {
                s = ChannelState {};
                return 0.0f;
            }

            return static_cast<float> (y);
        }

    private:
        struct ChannelState
        {
            double ic1 = 0.0;
            double ic2 = 0.0;
        };

        std::vector<ChannelState> state;
    };
}

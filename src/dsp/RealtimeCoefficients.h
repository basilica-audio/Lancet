#pragma once

#include <juce_dsp/juce_dsp.h>

#include <array>

// Real-time-safe biquad coefficient updates for juce::dsp::IIR::Filter.
//
// juce::dsp::IIR::Coefficients<float>::makeLowShelf/makeHighShelf/
// makePeakFilter/makeBandPass/... (the usual way to build filter
// coefficients) heap-allocate a brand new Coefficients object on every call
// - fine in prepareToPlay(), not fine on the audio thread where Lancet's
// six bands' freq/Q/gain can all be automated continuously.
//
// juce::dsp::IIR::ArrayCoefficients<float>::makeXxx returns the same
// coefficients as a std::array (stack storage, zero allocation). This
// header writes that array's values directly into an *already-allocated*
// Coefficients<float> object's raw coefficient storage (normalising by a0
// exactly the way Coefficients' own constructor/assignImpl does), so
// repeated calls during processBlock() never touch the heap. Same pattern
// as sibling plugin twist-your-guts's src/dsp/RealtimeCoefficients.h.
//
// JUCE 8.0.14, juce_dsp/processors/juce_IIRFilter.h (Coefficients::assignImpl
// shows the {b0,b1,b2,a1,a2} normalised-by-a0 storage layout this mirrors).
namespace lnct
{
    // Writes a normalised 2nd-order {b0,b1,b2,a1,a2} set (5 raw coefficients)
    // computed from a raw {b0,b1,b2,a0,a1,a2} array (as returned by
    // juce::dsp::IIR::ArrayCoefficients<float>::makeLowShelf/makeHighShelf/
    // makePeakFilter/makeBandPass/...) into `target`, which must already
    // hold a 2nd-order filter's coefficient storage (i.e. have been
    // constructed via the 6-argument Coefficients constructor, or a prior
    // makeXxx() call, at least once - typically during prepareToPlay()).
    // Divides each raw coefficient by a0 directly (rather than precomputing
    // invA0 = 1/a0 once and multiplying) so that a coefficient exactly
    // equal to a0 (as b0 is for a peaking/shelf filter at exactly 0 dB
    // gain, per the RBJ cookbook - see DynamicBand.cpp's class comment)
    // normalises to *exactly* 1.0. IEEE 754 division guarantees x/x == 1.0
    // exactly for any finite non-zero x, but the composition x*(1/x) does
    // not carry that same guarantee (the reciprocal 1/x is itself rounded
    // to the nearest representable float before the multiply, so the
    // product can land one ULP away from 1.0). That ULP-level slop was
    // measurable as a real (non-floor-clamped) roughly -90 dB deviation
    // across Lancet's six cascaded bands at unity gain - see
    // tests/NullTests.cpp's "on with Gain=0/Range=0" guarantee, which needs
    // <= -120 dBFS.
    inline void applyBiquadCoefficients (juce::dsp::IIR::Coefficients<float>& target,
                                          const std::array<float, 6>& raw) noexcept
    {
        jassert (target.getFilterOrder() == 2);

        auto* dest = target.getRawCoefficients();
        const auto a0 = raw[3];

        dest[0] = raw[0] / a0; // b0
        dest[1] = raw[1] / a0; // b1
        dest[2] = raw[2] / a0; // b2
        dest[3] = raw[4] / a0; // a1
        dest[4] = raw[5] / a0; // a2
    }
}

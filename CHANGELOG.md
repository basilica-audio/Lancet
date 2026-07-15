# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog 1.1.0](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.1.0] — 2026-07-15

### Added

- Project bootstrap: README, license, contributing guide, architecture and build docs, ADRs, and CI workflow.
- **DSP core (M1):** the full six-band dynamic EQ signal path per `docs/design-brief.md` - serial bell/shelf bands (Band 1 Low Shelf, Band 6 High Shelf, Bands 2-5 always Bell), each with its own pre-chain-tapped, cascaded-bandpass `Detector` driving a soft-knee (Giannoulis/Massberg/Reiss quadratic knee) gain computer clamped to the band's `Range`, real-time-safe `ArrayCoefficients`-based coefficient updates on a 32-sample sub-block granularity with `SmoothedValue`-ramped gain (zipper guard), Input/Output Trim, parallel `Mix` via `DryWetMixer`, and exclusive per-band `Listen` (sidechain solo of a band's own detector signal).
- `src/params/ParameterLayout.cpp`: the complete v0.1 APVTS parameter layout (59 parameters - 3 global + six bands' On/Type/Freq/Q/Gain/Range/Threshold/Attack/Release/Listen) with frozen IDs (`src/params/ParameterIds.h`).
- A functional v0.1 slider/toggle/combo-box editor covering every parameter (custom LookAndFeel is roadmap M3).
- `docs/manual.md`: full user manual (what Lancet is, where it sits in a mix chain, signal flow, complete parameter reference, usage tips).
- `docs/architecture.md`: full engineering writeup, including two real floating-point gotchas found and fixed while writing the M1 test suite - a biquad coefficient normalisation precision fix (`RealtimeCoefficients.h`, dividing directly rather than multiplying by a precomputed reciprocal) and an exact-0-dB filter bypass (compiler FMA-contraction defeats bit-exact TDF-II cancellation even when the coefficients are mathematically an exact identity).
- Catch2 suite: 47 test cases covering all 10 M1 guarantee categories (null/transparency, static magnitude vs. an independently-implemented analytic RBJ reference, dynamic gain convergence, detector band isolation >20 dB/2 oct, NaN/Inf recovery, oversized-block clamp, state round-trip, `reset()` clears state, zero latency, and an automation zipper guard), plus Listen, Mix, bus-layout/sample-rate sweep, and long-run stability coverage.

### Fixed

- `juce::dsp::Gain<float>` defaults its internal smoothed gain target to linear 0 (silence), not unity/0 dB, until a setter is called - `LancetEngine::prepare()` now re-primes Input/Output Trim from their last-commanded values immediately after `juce::dsp::Gain::prepare()`, matching the priming idiom already used for `Mix`. Never affected the shipped plugin (`LancetAudioProcessor` always pushes parameters before `prepare()`), but was a real footgun for `LancetEngine`'s own public API.

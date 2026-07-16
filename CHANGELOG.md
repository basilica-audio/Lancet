# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog 1.1.0](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.2.0] — 2026-07-16

### Added

- **Deep-dive voicing rework (`docs/design-brief.md`, sourced in `docs/research-notes.md`):** research-derived against the F6-class reference set (Waves F6, FabFilter Pro-Q 3/4, TDR Nova, Sonnox Oxford Dynamic EQ).
  - **Knee width now derives from Range** (`clamp(|rangeDb| * 0.5, 2, 10)` dB), replacing the v0.1.0 flat 6 dB constant - shallower Range settings read gentler, and the ±12 dB Range case reproduces the old fixed-6-dB knee's shape exactly (a deliberate, tested bit-compatible-in-shape special case).
  - **Program-dependent Auto Release** (`bN_autoRelease`, new per-band boolean, off by default): a dedicated fast reference envelope inside `Detector` measures the signal's own recent fall rate independently of the user's Release setting, deriving an effective release that is always `<=` the manual Release value and shortens automatically when the signal is already decaying on its own - inspired by, not a reproduction of, Waves F6's proprietary ARC (see the design brief's honesty section).
  - **Gain/Q coupling** (`bN_gainQ`, new per-band boolean, off by default): the main filter's own Q widens (softens) proportionally to how far the band's *dynamic* gain sits toward Range, an opt-in analog-style softening character switch (Sonnox Oxford Dynamic EQ's documented "Q reduces with gain" behaviour) scoped to the audible filter shape only, never the Detector's own sidechain selectivity.
  - **Attack range widened** to 0.1-500 ms (was 0.5-100 ms) and **Release range widened** to 5-1500 ms (was 10-1000 ms), both ends, reaching faster transient-catching and slower musical tonal-balancing use cases documented in the reference class.
  - State migration is tolerant: a v0.1.0 session (missing the two new per-band IDs) loads cleanly with them at their off default, and every pre-existing parameter value (including Attack/Release values that now sit inside the widened ranges) is preserved exactly.
- **M2 preset system** (`src/presets/`, copied verbatim from the `basilica-audio/nave` pilot per its `docs/preset-system-notes.md` replication recipe): `PresetManager`/`PresetBar`/`Localisation`, a preset bar docked at the top of the editor (Save/Save As/Delete/Import/Export, factory + user library, "Set current as default"), single-file and zip-bank import/export.
- **Nine factory presets** (`presets/factory/*.json`, documented one-line-each in `docs/presets.md`): Default, Gentle Glue, De-Ess Stack, Transient Snare Crack, Mix Buss Settle, Slow Tonal Ride, Chest Resonance Tamer, Fast-Recovery Demo, Listen Check.
- **German frame-string localisation** (`resources/i18n/de.txt`) - preset bar/dialog strings only; core DSP terminology (parameter names, units) is never translated, in any language.
- App icon wired via `ICON_BIG` (`juce_add_plugin`) - this repo never got a patch release with the icon fix the rest of the suite received.
- The suite-wide `create-release`-job fix folded into `.github/workflows/release.yml` (idempotent release-object creation before the macOS/Windows jobs upload assets, `find`-based artefact-directory lookup) - this repo never got the earlier ci-fix PR.
- Catch2 suite grown to 87 test cases: the new deep-dive DSP guarantees (knee-width curve proof calibrated against the Detector's own measured insertion loss, program-dependent-release proof at both the `Detector` and `LancetEngine` level, gain/Q-coupling bandwidth proof via `DynamicBand`'s independent trigger/probe arguments, Attack/Release range-boundary proofs, tolerant-state-import proof, a de-essing preset spectral proof), the full M2 preset-system suite (16 cases, adapted from the nave pilot), i18n frame coverage, and a permanent audio-thread `AllocationGuard`/`AllocationTests.cpp` regression harness (new to this repo, extended to cover the auto-release and gain/Q-coupling code paths specifically) referenced by sibling suite rebuilds as their own `AllocationGuard` pattern source.

### Changed

- Version bumped to 0.2.0 (`CMakeLists.txt`).

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

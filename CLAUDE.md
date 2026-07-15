# Lancet — surgical dynamic EQ (mix)

Per-repo working memory for Claude Code sessions on this plugin. Part of the **Basilica Audio** plugin suite — sacred-architecture DSP for heavy music (`github.com/basilica-audio`).

## What this is
Lancet is the suite's dynamic EQ: up to six surgical bands whose gain moves with the program material — named after the lancet window, the narrowest of Gothic arches. AU / VST3 / Standalone, JUCE 8.

## Status (v0.1.0 — M1 DSP completion & test coverage done)
Core DSP complete for v0.1.0, **47/47 Catch2 tests green** locally. Six-band dynamic EQ engine (`src/dsp/LancetEngine`, `DynamicBand`, `Detector`) fully wired: per-band bell/shelf filters, cascaded-bandpass detectors, soft-knee gain computer, 32-sample sub-block coefficient updates, Input/Output Trim, parallel Mix, exclusive Listen. GUI is a functional v0.1 slider/toggle/combo-box editor covering every parameter (custom LookAndFeel is roadmap M3). No signing yet (roadmap M4). See `docs/design-brief.md` for the binding M1 specification and `docs/architecture.md` for the full engineering writeup (including two floating-point precision gotchas found while writing the test suite).

## Design principles (Yves, binding)
- **Readability of control state**: users must see at a glance where every control sits — high-contrast pointer knobs, engraved scale rings, one analog gain-reduction needle per band (M3).
- Vintage/photoreal skeuomorph GUI at M3 per the suite-wide `basilica-gui-design` skill; v0.1 ships the functional slider editor.

## DSP
`in -> [Input Trim] -> Band1 -> Band2 -> Band3 -> Band4 -> Band5 -> Band6 -> [Mix] -> [Output Trim] -> out`, with every band's `Detector` tapping the pre-chain signal (right after Input Trim, before Band 1) rather than the evolving in-place signal. Each `DynamicBand` (`src/dsp/DynamicBand.{h,cpp}`) is an RBJ bell (Bands 2-5) or bell/shelf (Band 1 Low Shelf, Band 6 High Shelf) filter via `ArrayCoefficients` (stack, no heap), gain = static Gain + a soft-knee-clamped dynamic term derived from its `Detector` (`src/dsp/Detector.{h,cpp}`: a cascaded 2x RBJ bandpass, >20 dB/2oct selective at Q=1, feeding a linked stereo peak envelope). Coefficients/gain update once per 32-sample sub-block via a `SmoothedValue` ramp (zipper guard). Zero latency (all biquads, no lookahead). Two floating-point gotchas found and fixed while writing the M1 suite - see `docs/architecture.md`'s "Normalisation detail" and "The exact-0-dB bypass" sections, plus "A related gotcha: juce::dsp::Gain's silent default".

## Build & test
```sh
export CPM_SOURCE_CACHE="$HOME/.cache/CPM"      # shared JUCE 8.0.14 + Catch2 cache
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target Tests Lancet_Standalone --parallel 4
ctest --test-dir build --output-on-failure
```
Release/universal + pluginval + auval run in CI, not locally.

## Conventions & guardrails
- JUCE 8.0.14 via CPM · C++20 · AGPLv3 · Pamplejuce `SharedCode` pattern · manufacturer `Yvsv`, plugin code `Lnct`, `com.yvesvogl.lancet`.
- **Real-time safety:** no alloc/lock/file-IO/logging on the audio thread; allocate in `prepareToPlay`; `reset()` clears ALL state (suite review finding: missing resets ship real bugs); `ScopedNoDenormals`; smoothed params.
- **Filter coefficients on the audio thread:** use `juce::dsp::IIR::ArrayCoefficients` (stack, no heap) — `Coefficients::make*` allocates and is a known suite-wide review finding.
- **Guard oversized blocks in Release builds** (real clamp, not only `jassert` — asserts compile out).
- **DryWetMixer gotcha (JUCE 8.0.14):** prime `setWetMixProportion(mix)` before `reset()` in `prepare()`.
- **`main` is protected** — no direct commits; feature branch + PR, green CI required (Conventional Commits). New DSP needs tests (null/reference, NaN/Inf sweep, state round-trip, latency).

## Roadmap
GitHub milestones (M1 DSP & tests · M2 presets/state · M3 GUI & a11y · M4 release/signing/v1.0.0) + issues. Read with `gh issue list --repo basilica-audio/lancet`.

## Suite context
Style references: siblings `basilica-audio/Overture` and `basilica-audio/Triptych` (band splitting/compression) and `basilica-audio/Silentium` (envelope followers). The suite: overture, tenebrae, nave, silentium, requiem, seraph, aureate, firmament, triptych, apotheosis, crypta, lancet, miserere.

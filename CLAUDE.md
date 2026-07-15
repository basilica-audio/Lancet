# Lancet — surgical dynamic EQ (mix)

Per-repo working memory for Claude Code sessions on this plugin. Part of the **Basilica Audio** plugin suite — sacred-architecture DSP for heavy music (`github.com/basilica-audio`).

## What this is
Lancet is the suite's dynamic EQ: up to six surgical bands whose gain moves with the program material — named after the lancet window, the narrowest of Gothic arches. AU / VST3 / Standalone, JUCE 8.

## Status (M0 — bootstrap)
Skeleton scaffolded; M1 DSP implementation in progress. See `docs/design-brief.md` for the binding M1 specification (topology, parameters, tests).

## Design principles (Yves, binding)
- **Readability of control state**: users must see at a glance where every control sits — high-contrast pointer knobs, engraved scale rings, one analog gain-reduction needle per band (M3).
- Vintage/photoreal skeuomorph GUI at M3 per the suite-wide `basilica-gui-design` skill; v0.1 ships the functional slider editor.

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

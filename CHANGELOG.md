# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog 1.1.0](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Changed

- **The suite now presents itself as Basilica Audio in every host.** `COMPANY_NAME` moves from
  `Yves Vogl` to `Basilica Audio`, so Lancet files under the brand in Logic's plugin manager,
  Cubase's vendor column and Reaper's FX browser instead of under a person's name. **Plugin
  identity is untouched** and no session is affected: the VST3 class ID derives from
  `PLUGIN_MANUFACTURER_CODE` + `PLUGIN_CODE` alone (JUCE 8.0.14, `juce_VST3ModuleInfo.h`'s
  `VST3Interface::jucePluginId`) and the Audio Unit triple stays `(aufx, <PLUGIN_CODE>, Yvsv)` -
  both diffed on a real build before and after the change. The bundle ID stays
  `com.yvesvogl.lancet` on purpose, because changing it is what would break existing projects, and
  `COMPANY_COPYRIGHT` still names the copyright holder rather than the trading name. See
  [`docs/branding.md`](docs/branding.md) and basilica-audio/.github ADR 0001.
- **User presets now live under `Basilica Audio`, and the ones you already saved come with them.**
  The folder moves to `~/Library/Audio/Presets/Basilica Audio/Lancet/` (macOS) and
  `%APPDATA%\Basilica Audio\Lancet\Presets\` (Windows). On first launch `PresetManager` copies
  every preset out of the old `Yves Vogl` folder into the new one. It **copies rather than moves**,
  so an older build - or a downgrade - still finds its presets where it left them, and it never
  overwrites a file already present under the new name. Nothing is deleted, ever.
- **Plugin metadata now carries the vendor URL, the copyright string, a real description and
  the VST3 sub-category.** `COMPANY_WEBSITE`, `COMPANY_COPYRIGHT` and `DESCRIPTION` were never
  set, so a shipped bundle carried an empty `NSHumanReadableCopyright`, an empty VST3 vendor
  URL, and an AU `description` that was just the plugin name again; `VST3_CATEGORIES` fell back
  to JUCE's bare `Fx` default, which filed every plugin in the suite under the same
  undifferentiated heading in a VST3 host's browser. Lancet now declares
  `Fx EQ Dynamics` (JUCE 8.0.14, `juce_add_plugin`). **Plugin identity is unchanged** — the VST3 class
  ID is derived from `PLUGIN_MANUFACTURER_CODE` + `PLUGIN_CODE` alone
  (`juce_VST3ModuleInfo.h`'s `VST3Interface::jucePluginId`) and the AU type/subtype/manufacturer
  triple is untouched, so existing sessions keep resolving to the same plugin.

### Fixed

- **Release notes are the changelog again, not a list of PR titles.** `release.yml` now builds the
  release body from this file's section for the tag being released, via the suite-wide
  `basilica-audio/.github/release-notes` action, and appends what a downloader actually needs: what
  each archive contains, the signing status per platform stated accurately (macOS signed, notarised
  and stapled; Windows **not** code-signed, so SmartScreen will warn), the install paths, the AU
  rescan hint, and links to the manual and the product page. A tag whose version has no section in
  this file now fails the release job rather than publishing an empty page.
- **The manual's caveat list no longer says the binaries are unsigned.** macOS release
  bundles are Developer-ID-signed, notarised and stapled; Windows is not yet
  Authenticode-signed, which is what the caveat now says.
- **The README no longer tells users the binaries do not exist.** The Installation section
  said *"No pre-built binaries are published yet"* while the banner four lines above it linked
  the Releases page, and the banner in turn described the macOS builds as *"currently
  unsigned"*. Both claims were false. The Installation section now describes the actual
  download-and-copy flow, and the banner states what the release workflow actually produces:
  verified against the shipped `v0.5.0` `.component` with `codesign --verify --strict`
  (`Developer ID Application: Yves Vogl (M5WT732AY5)`), `spctl -a -t open`
  (`source=Notarized Developer ID`) and `stapler validate`.
- **The documented factory-preset count matches what ships** (nine -> eleven (manual)); `presets/factory/` holds 11.
- **Removed committed scratch/diagnostic test files** that were documented in their own
  headers as throwaway probes: `tests/ZZDebugAutoRelease.cpp`, `tests/ZZDumpXmlTest.cpp`, `tests/ZZProbe.cpp`.

### Added

- **A `Documentation` section in the README** pointing at the user manual, the factory-preset
  reference, the changelog and the product page — the manual was only reachable from a
  sentence in the middle of the Signal flow section.

## [0.5.0] — 2026-08-20

### Changed

- **Per-band default Thresholds are now calibrated by measurement** (the final
  issue #4 voicing item, `docs/voicing-notes.md` Addendum): each band's default
  Threshold is set to that band's own measured detector level under a defined
  programme-level anchor (band-limited 20 Hz–20 kHz pink noise at -18 dBFS RMS,
  the common alignment-level convention), rounded to 1 dB — Band 1: -26 → **-24 dB**,
  Band 2: -28 → **-25 dB**, Band 3: -26 → **-24 dB**, Band 4: -24 dB (unchanged),
  Band 5: -22 → **-24 dB**, Band 6: -20 → **-24 dB**. Rationale: the detector's
  constant-Q bandpass on pink noise settles at ≈ -24 dBFS for *every* band
  (equal energy per octave × frequency-independent relative bandwidth), so the
  v0.3.0 spread — chosen, per that release's own honesty section, "to spread
  evenly between -20 and -28" without a measurement — accidentally produced the
  opposite of its intent: Band 2 was already 3.2 dB into overshoot at reference
  level while Band 6 needed material 4.3 dB above reference to move at all, a
  7.5 dB accidental spread in engagement loudness. Every band now begins
  engaging at the same programme loudness out of the box.

  **This changes no default sound**: Range still defaults to 0 dB on every band,
  so the default state remains bit-transparent (the unchanged
  `tests/NullTests.cpp` guarantee) — only the point at which a freshly dialed-in
  Range starts acting moves. Sessions and presets are unaffected (they store
  their own Threshold values; the tolerant-import guarantee is unchanged).
  Measured and frozen by the new `tests/ThresholdCalibrationTests.cpp`: per-band
  |measured − default| ≤ 1 dB at 44.1/48 kHz (1.5 dB at 96 kHz), cross-band
  onset-gap spread ≤ 1.5 dB, and a default-band engagement proof (gentle,
  sub-1-dB-average action at the anchor with the design brief's sourced -6 dB
  starting Range; zero action 12 dB below it).

### Added

- **Control-law freeze** (`tests/ControlLawTests.cpp`): the knob-travel analysis
  for issue #4 *confirmed* the shipped mappings rather than changing them — the
  log Freq range centres on the 630 Hz default-on demo band (mid-travel =
  √(20·20000) = 632.5 Hz), the log Attack/Release ranges put the design brief's
  sourced starting recipe (10 ms / ~100 ms) at 0.53–0.54 travel, the Q skew
  centres the musical 0.7–4 window, and every per-band voiced default sits in
  the middle half of its knob's travel. These properties are now pinned by
  tests, because a silent skew/range edit would re-curve every host automation
  lane written against the parameter (the automation-mapping hazard the v0.4.0
  state-schema note documents for the still-deferred Range/Q range widening).
- `docs/voicing-notes.md` Addendum: the calibration method, the
  old-vs-measured-vs-new table, considered-and-rejected items (defaulting the
  outer bands' Type to Shelf would ship a dead Q knob, violating the binding
  "readability of control state" principle), and an honesty section (the anchor
  is a convention-backed proxy, not per-genre programme material; the issue's
  original by-ear reference-class A/B remains undone for lack of access, named
  rather than silently dropped). Catch2 suite: 148 → 155 cases.

## [0.4.0] — 2026-07-27

### Fixed

- **The Attack knob now actually works below ~50 ms.** Up to v0.3.0 a band's realised gain came from a fixed 50 ms `juce::SmoothedValue` sitting *after* the gain computer, so the dialed Attack could only ever be as fast as that smoother allowed. The shipped range has gone down to 0.1 ms since v0.2.0, which means most of it did nothing: 0.1 ms, 1 ms and 20 ms all sounded the same. v0.4.0 removes the smoother entirely and evaluates the whole chain - detector envelope, dB conversion, soft-knee gain computer, filter gain - **once per sample**, so the detector's own ballistics are the only thing shaping how fast a band moves.

  **This changes how existing sessions sound.** A session that used a non-zero Range together with an Attack faster than ~50 ms *will* react faster than it did under v0.3.0. That is the defect being fixed, not a new voicing decision, and it is the only behavioural change in this release: a session with `Range = 0` on every band (i.e. a pure static EQ) nulls against v0.3.0 to within float rounding. All ten pre-existing factory presets were re-validated against the new path and none of their stored values were changed - their Attack/Release settings simply take effect now, which makes each of them sound more like its own name.
- `DynamicBand::prepare()` re-primed its detector with hardcoded 5 ms/150 ms ballistics, discarding whatever Attack/Release had been set. In the plugin this cost one block after every `prepareToPlay()` (the next parameter push corrected it); anything driving `LancetEngine` directly was silently pinned at 5 ms indefinitely. Each band now remembers both values and re-derives their coefficients at the new sample rate.

### Added

- **Ballistics-true per-sample gain path on a trapezoidal SVF core** (`src/dsp/TptSvf.h`). Removing the gain smoother from a coefficient-rebuilding biquad would have been a zipper-noise generator, so the band's filter changed at the same time: a topology-preserving-transform state-variable filter (Simper/Cytomic trapezoidal integration, implemented from the published equations - no third-party code vendored) whose integrator state keeps its meaning under per-sample coefficient modulation. Bells and shelves alike take the per-sample path; there is no stepped-gain fallback anywhere.

  The realised static response is *identical* to the RBJ biquad it replaces, so existing static EQ settings keep exactly the curve they had: verified against an independent double-precision RBJ reference to better than -100 dBFS peak residual over 10 s of broadband noise per setting, and against the analytic RBJ magnitude response to ±0.05 dB across a band-type/gain/Q grid. Integrator state is kept in double precision so that low-frequency, high-Q bands stay accurate while being modulated every sample.
- **External sidechain input** (`bN_scSource`, per band, Internal/External, default Internal). An optional second input bus, declared disabled by default so a host that ignores it sees the v0.3.0 I/O signature unchanged. A band set to External detects from the sidechain instead of the pre-chain tap, and **falls back to Internal** whenever the host provides no sidechain - selecting it in a host without sidechain routing is inaudible rather than silent. No alignment delay is inserted: Lancet stays at zero latency, so the sidechain must be time-aligned by the host, as comparable dynamic EQs require.
- **Split/Wide detection** (`bN_scMode`, per band, default Split). Split is the band-matched cascaded bandpass every previous version always used; Wide bypasses it so the band follows full-range programme level - the standard way to make one band duck against the whole mix. Switching Wide → Split re-primes the bandpass rather than resuming from stale state, and neither switch ever resets the envelope, so both are click-free mid-playback. Listen follows the selection: it always auditions the feed the detector is actually hearing.
- **Anti-aliased saturation.** The per-band Saturation stage's memoryless `tanh` is replaced by a first-order antiderivative-antialiased (ADAA1) kernel of the same shape (`src/dsp/AdaaTanh.h`, Parker/Zavalishin/Esqueda DAFx-16 lineage, implemented from the published equations) - same harmonic character, less fold-back, still zero latency and no oversampling stage. Measured suppression of the dominant 30 kHz → 18 kHz fold at 48 kHz: **13.2 dB** at -24 dBFS, **10.0 dB** at -12 dBFS, **8.4 dB** at -6 dBFS input, with total in-band alias power improving by 13.2 / 10.2 / 9.2 dB at the same levels. No absolute alias floor is claimed: base-rate ADAA1 has no validated absolute tier in the research this drew on, so the specification is deliberately relative and level-pinned.
- **Smoothed Gain/Q coupling.** The coupling now reads a 30 ms-smoothed copy of the dynamic gain rather than the raw instantaneous value, so the realised Q glides instead of taking a full-range step at a sub-block boundary. Measured: never more than 0.5 Q units of change per 32-sample sub-block at base Q up to 12, while still traversing the full documented `baseQ → 0.4·baseQ` span.
- **Per-band telemetry** for the planned M3 gain-reduction needle: `LancetEngine::getLastAppliedDynamicGainDb()` and `getLastAppliedFilterQ()`, both relaxed atomics written once per block. No meter uses them yet; the first is checked against the gain reduction actually measurable in the output.
- **State schema versioning.** Saved state now carries a `stateVersion="2"` attribute on its root; a state without one is read as schema 1 (everything up to v0.3.0). Schema 2 needs no value transform - every parameter added since schema 1 defaults to the behaviour that predates it - and the stamp exists so a future release that *does* need one (the planned Range/Q range widening changes host automation-curve mapping) has somewhere to branch. Backed by a checked-in, human-readable `tests/fixtures/state-v0.3.0.xml`.
- **An eleventh factory preset, "Sidechain Carve"** (`presets/factory/sidechainCarve.json`): Band 3 cutting against an external sidechain in Wide mode, the discoverable demonstration of the new routing.
- Two combo boxes per band column in the editor (SC Source, SC Mode). The rest of the v0.2.0/v0.3.0 per-band booleans stay automation- and preset-only until the M3 GUI pass; these two get controls because a sidechain routing that cannot be selected from the editor is not usable at all.
- Catch2 suite grown to 145 cases: `tests/AttackFidelityTests.cpp`, `tests/SidechainTests.cpp` and `tests/AliasingTests.cpp` are new, and the static-response, null, dynamic-behaviour, zipper, Gain/Q, state, tolerant-import, latency, allocation, robustness, preset and engine suites were all extended.

### Changed

- **Idle coefficient recompute skipped.** The detector's bandpass and the SVF's frequency warp are recomputed only when the smoothed frequency/Q have actually moved, instead of unconditionally every sub-block, and the per-sample SVF scalars are memoised on an exact gain match. Bit-transparent once parameters are static.
- Version bumped to 0.4.0 (`CMakeLists.txt`).

## [0.3.0] — 2026-07-23

### Added

- **Musical defaults and character pass** (issue #4, `docs/voicing-notes.md`): honestly documented as a mix of measured DSP regression proofs and by-ear/judgment-tuned numbers - see that document's Honesty section for exactly which is which.
  - **Per-band default Q/Threshold/Attack/Release**, tuned to each band's typical role along the existing frequency ladder (was a flat Q 1.0/Threshold -30 dB/Attack 5 ms/Release 150 ms for every band): Band 1 (100 Hz, boom/sub control) now starts slow and gentle (Attack 25 ms/Release 280 ms); Band 5 (4 kHz, sibilance/harshness) starts fast (Attack 2 ms/Release 70 ms); Bands 2-4 step down progressively; Band 6 (air/fizz recovery shelf) sits close to Band 5. Range stays 0 dB (idle) for every band regardless - nothing moves until a Range is dialed in. The resulting envelope-follower ballistics ordering (not just the numeric defaults) is measured and frozen by `tests/BallisticsDefaultsTests.cpp`.
  - **Gentle Saturation** (`bN_sat`, new per-band boolean, off by default): a soft `tanh`-based waveshaper applied to a band's own output, but only while the band is actively boosting (static + dynamic gain net positive) - a cutting or idle band is untouched even with Saturation on. Drive scales with how hard the band is boosting. State migration is tolerant (a pre-v0.3.0 session missing `bN_sat` loads with it at its off default). Measured via a correlation-based (FFT-free) distortion proof in `tests/SaturationTests.cpp`: added harmonic energy while boosting with Saturation on, bypass verified while cutting or idle.
  - **A tenth factory preset, "Analog Warmth Lift"** (`presets/factory/analogWarmthLift.json`): Band 2 gentle low-mid boost demonstrating the new Saturation toggle.
  - **`docs/voicing-notes.md`**: full reasoning for every change above, an explicit honesty section (the per-band ballistics/Threshold numbers and the saturation drive curve are engineering judgment, not sourced from a reference plugin or a real-material listening session), and a "what remains" section naming the by-ear reference-class comparison issue #4 originally asked for as the clearest open item for a future pass.
- Catch2 suite grown further: `tests/BallisticsDefaultsTests.cpp` (per-band default value freeze + measured Attack/Release step-response ordering) and `tests/SaturationTests.cpp` (correlation-based distortion proof, boosted-only bypass proof, idle-bypass proof, Gain-ceiling robustness), plus extended coverage of the existing allocation guard, state round-trip, tolerant-import, and randomised-parameter-sweep robustness suites for the new `bN_sat` parameter.

### Changed

- Version bumped to 0.3.0 (`CMakeLists.txt`).

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

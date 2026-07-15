<p align="center"><img src="docs/assets/icon.png" alt="Lancet icon" width="160"/></p>

# Lancet

*Cut where it counts — a surgical dynamic EQ with an analog soul.*

[![CI](https://github.com/basilica-audio/lancet/actions/workflows/ci.yml/badge.svg)](https://github.com/basilica-audio/lancet/actions/workflows/ci.yml)
[![License: AGPL v3](https://img.shields.io/badge/License-AGPL%20v3-blue.svg)](https://www.gnu.org/licenses/agpl-3.0)

> **Work in progress.** Lancet is pre-1.0 and under active development. Binaries for macOS and Windows are available from the [Releases](../../releases) page (currently unsigned — see the release notes); building from source works too. Expect breaking changes until v1.0.0 ships (see [Roadmap](#roadmap)).

<!-- ==BEGIN BODY== (plugin engineer: replace this block with What it is / Features / Signal flow / Roadmap) -->
## What it is

Lancet is a six-band dynamic EQ built on JUCE 8, in the spirit of the Waves F6 class, voiced for heavy mixes. Each band is a normal parametric EQ band (bell, or shelf on Band 1/Band 6) whose gain can also move with the program material: a per-band detector, band-filtered from the plugin's pre-EQ input, drives a soft-knee gain computer that cuts or boosts on top of the band's static setting once the signal crosses its threshold. Because every detector taps the same unperturbed pre-chain signal rather than the evolving, serially-processed audio, one band's move never confuses another band's detection, and a band never triggers itself. See [`docs/manual.md`](docs/manual.md) for the full user manual.

## Features (v0.1.0 scope)

- **Six serial bands** (Bell on all six; Band 1 additionally offers a Low Shelf, Band 6 a High Shelf), each with:
  - **Freq** - 20 Hz - 20 kHz, log-skewed
  - **Q** - 0.3 - 12 (fixed at the standard 0.707 shelf slope in Shelf mode)
  - **Gain** - static gain, -12 to +12 dB
  - **Range** - dynamic depth on top of Gain, -12 to +12 dB (0 = pure static band); negative cuts as the signal gets louder, positive boosts as it gets louder
  - **Threshold** - detector threshold, -60 to 0 dB, with a 6 dB soft knee
  - **Attack** / **Release** - 0.5 - 100 ms / 10 - 1000 ms
  - **Listen** - exclusive sidechain solo of that band's own bandpass-filtered detector signal, for auditioning exactly what triggers it
- **Per-band detector isolation** - a cascaded (4th-order effective) bandpass matched to each band's own frequency/Q, measured at >20 dB attenuation two octaves from centre at Q=1, so a loud out-of-band tone doesn't falsely trigger a band
- **Input Trim** and **Output Trim** - global gain stages before Band 1 and after the Mix blend, -12 to +12 dB
- **Mix** - parallel dry/wet blend of the whole six-band chain, 0-100%
- **Zero added latency** - every filter (bands and detectors) is a minimum-phase IIR biquad with no lookahead, so no dry-path delay compensation is needed anywhere in the plugin
- Full state save/recall via `AudioProcessorValueTreeState`

## Signal flow

```
in --[Input Trim]--+--[pre-chain tap]--> each band's Detector (bandpass @ band freq/Q -> envelope)
                    |
                    +--> Band1 -> Band2 -> Band3 -> Band4 -> Band5 -> Band6 --> [Mix] --> [Output Trim] --> out
```

See [`docs/architecture.md`](docs/architecture.md) for the full breakdown, including the soft-knee gain-computer formula, detector selectivity, sub-block coefficient smoothing and the zipper guard, Listen semantics, and a couple of real floating-point gotchas found (and fixed) while writing the M1 test suite.

## Roadmap

| Milestone | Description | Status |
|---|---|---|
| M0 | Bootstrap - project skeleton, CI, docs | Done |
| M1 | DSP completion & test coverage - six-band dynamic EQ engine, soft-knee gain computer, Listen, broad Catch2 suite | Done |
| M2 | Presets & state recall | Planned |
| M3 | Custom GUI & accessibility | Planned |
| M4 | Release engineering - signing, notarization, installers, v1.0.0 | Planned |
<!-- ==END BODY== -->

## Installation

No pre-built binaries are published yet (see the work-in-progress notice above). Once releases begin, installation will follow the standard plugin locations:

**macOS**

| Format | Path |
|---|---|
| AU (Component) | `~/Library/Audio/Plug-Ins/Components/` |
| VST3 | `~/Library/Audio/Plug-Ins/VST3/` |

If Logic Pro doesn't pick up the plugin after installing, force a rescan by resetting the AU cache:

```sh
killall -9 AudioComponentRegistrar
auval -a
```

**Windows**

| Format | Path |
|---|---|
| VST3 | `C:\Program Files\Common Files\VST3\` |

## Building from source

Requires JUCE 8.0.14, C++20, and CMake ≥ 3.24. See [`docs/building.md`](docs/building.md) for full prerequisites and step-by-step build/test commands for macOS and Windows.

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

## License

Lancet is licensed under the [GNU Affero General Public License v3.0](LICENSE) (AGPLv3).

This project uses [JUCE](https://juce.com) 8, whose open-source tier is licensed under AGPLv3 (as of JUCE 8; JUCE 7 and earlier used GPLv3), which is why this project is AGPLv3 rather than GPLv3. See [`docs/adr/0002-agplv3-licensing.md`](docs/adr/0002-agplv3-licensing.md) for the full reasoning.

VST is a registered trademark of Steinberg Media Technologies GmbH.

Lancet is an independent open-source project and is not affiliated with, endorsed by, or sponsored by any plugin manufacturer.

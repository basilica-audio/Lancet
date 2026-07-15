# Lancet — M1 Design Brief (binding)

Dynamic EQ in the spirit of the Waves F6 class, voiced for heavy mixes. Six bands whose
gain moves with the program material. This document is the binding M1 spec; deviations
need an ADR.

## Signal topology

```
in ── [Input Trim] ─┬─ Band 1 ─ Band 2 ─ Band 3 ─ Band 4 ─ Band 5 ─ Band 6 ─ [Output Trim] ── out
                    │   (serial minimum-phase IIR bells/shelves, gain per band =
                    │    static gain + dynamic gain from that band's detector)
                    └─ per-band sidechain: input tapped PRE-chain, band-filtered → envelope
```

- Bands process **serially** (standard parametric EQ). Each band's *detector* taps the
  **plugin input** (pre-EQ) filtered through a bandpass matched to the band's freq/Q, so
  detection is stable regardless of other bands' moves.
- Per-band filter: TPT state-variable (juce::dsp::StateVariableTPTFilter or direct SVF)
  or RBJ bell via **ArrayCoefficients** (no heap on the audio thread). Bands 1/6 offer
  shelf mode.
- Dynamic gain: envelope (peak, attack/release ballistics) vs threshold → overshoot
  in dB × ratio-style scaling, clamped to **Range** (negative Range = cut when loud,
  positive = boost when loud, i.e. upward/downward per band). Soft knee 6 dB.
  Coefficient update per 32-sample sub-block with smoothed gain (no zipper).

## Parameters (APVTS, per band b1..b6 unless noted)

| ID | Range | Default | Notes |
|---|---|---|---|
| `bN_on` | bool | off (b3: on) | band enable |
| `bN_type` | Bell / Shelf | Bell | only b1 (LowShelf) and b6 (HighShelf) expose Shelf |
| `bN_freq` | 20–20k Hz log | 100/250/630/1.6k/4k/10k | |
| `bN_q` | 0.3–12 | 1.0 | ignored in shelf mode (fixed 0.707) |
| `bN_gain` | ±12 dB | 0 | static gain |
| `bN_range` | ±12 dB | 0 | dynamic depth; 0 = static band |
| `bN_thresh` | −60–0 dB | −30 | detector threshold |
| `bN_attack` | 0.5–100 ms | 5 | |
| `bN_release` | 10–1000 ms | 150 | |
| `bN_listen` | bool | off | solo the band's sidechain bandpass (exclusive) |
| `in_trim`, `out_trim` | ±12 dB | 0 | global |
| `mix` | 0–100 % | 100 | global parallel blend (DryWetMixer, prime-before-reset) |

## Guarantees & tests (Catch2, ≥30 cases)

1. **Null:** all bands off (or on with gain=0, range=0) → bit-transparent apart from trim
   (assert ≤ −120 dBFS diff at unity trim).
2. **Static correctness:** band boost/cut magnitude at center frequency within ±0.5 dB of
   the analytic RBJ response for representative (freq, Q, gain) triples.
3. **Dynamic behavior:** sine at band center, level stepped above threshold → measured
   band gain approaches `range` (± 1 dB) after 5× release time; below threshold → static.
4. **Detector isolation:** a loud out-of-band tone must not trigger the band (>20 dB/oct
   selectivity at 2 octaves for Q=1).
5. NaN/Inf input sweep → finite output, state recovers after reset().
6. Oversized-block clamp (larger than prepared) → no crash, correct audio (Release-safe).
7. State round-trip: save → reload → identical parameter values.
8. `reset()` clears envelopes and filter state (feed impulse, reset, verify silence).
9. Latency: `getLatencySamples() == 0`.
10. Automation smoothness: full-range gain sweep over 1 s produces no sample-to-sample
    jump > 3 dB (zipper guard).

## Explicitly out of scope for M1
Spectrum analyzer display, external sidechain, M/S per band, lookahead, oversampling,
photoreal GUI (M3 — v0.1 ships the standard slider editor like all siblings).

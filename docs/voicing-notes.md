# Lancet — voicing notes

## Addendum (unreleased): measured threshold calibration and control-law freeze — closing issue #4

Scope: the final pass on issue #4, layered on top of the v0.3.0 pass
documented in the sections below (which remain unchanged as the historical
record of that release; their Threshold reasoning is superseded by this
addendum).

### A.1 Threshold defaults are now calibrated by measurement

The v0.3.0 honesty section (§3, below) named the per-band Threshold defaults
as the softest-sourced numbers of that pass — "chosen mostly to spread evenly
between -20 dB and -28 dB rather than from any specific measurement" — and
its §4 explicitly asked for real-material validation of exactly these
numbers. This pass performs that validation with a defined, reproducible
programme-level anchor and finds the v0.3.0 spread was not just unsourced but
**inverted relative to its own intent**.

**The anchor**: band-limited (20 Hz – 20 kHz) pink noise at -18 dBFS RMS —
pink noise as the standard broadband programme proxy (equal energy per
octave, roughly the long-term average tilt of mixed music), -18 dBFS RMS as
the common digital alignment-level convention. Synthesised in the frequency
domain (|X| ∝ 1/√f, seeded random phases) so the spectrum is exactly pink at
every sample rate measured.

**The measurement**: each band's own shipped Detector (Split-mode cascaded
bandpass at the band's default Freq/Q, peak envelope at the band's default
Attack/Release) was fed the anchor and its settled envelope level averaged
over ~10 s. Because the detector bandpass is constant-Q (relative bandwidth
independent of centre frequency) and pink noise carries equal energy per
octave, **every band sees approximately the same in-band level at the
anchor** — measured ≈ -24 dBFS across all six bands, stable within ±0.3 dB
across 44.1/48/96 kHz (the largest outlier is Band 6 at 96 kHz, +0.7 dB from
reduced bilinear frequency warping at 10 kHz):

| Band | v0.3.0 Threshold | Measured detector level at anchor (48 kHz) | New Threshold |
|---|---|---|---|
| 1 (100 Hz) | -26 dB | -24.1 dBFS | **-24 dB** |
| 2 (250 Hz) | -28 dB | -24.8 dBFS | **-25 dB** |
| 3 (630 Hz) | -26 dB | -23.9 dBFS | **-24 dB** |
| 4 (1.6 kHz) | -24 dB | -23.9 dBFS | **-24 dB** |
| 5 (4 kHz) | -22 dB | -24.3 dBFS | **-24 dB** |
| 6 (10 kHz) | -20 dB | -24.3 dBFS | **-24 dB** |

**What the old spread actually did**: v0.3.0 intended thresholds to "engage
a bit later climbing the ladder". But since every band's detector sits at
≈ -24 dBFS at reference level, the -28…-20 dB spread translated into onset
*loudness* differences of the opposite shape: Band 2 (-28 dB) was already
3.2 dB into overshoot — actively working — on reference-level material, while
Band 6 (-20 dB) needed material 4.3 dB *above* reference before moving at
all. A 7.5 dB accidental spread in engagement loudness, in a direction nobody
chose.

**The new rule**: each band's default Threshold equals its own measured
detector level at the anchor, rounded to the nearest 1 dB. Every band
therefore begins engaging at the same programme loudness out of the box, and
the per-band 1 dB differences that remain (Band 2's -25 dB) are *measured*
consequences of that band's Q/ballistics, not aesthetic spread. At the anchor
itself a band with Range dialed in engages gently through the soft knee
(measured: Band 3 at the design brief's sourced -6 dB starting Range averages
well under 1 dB of gain reduction at the anchor, and applies none at all
12 dB below it).

**Measured, not just asserted**: `tests/ThresholdCalibrationTests.cpp`
re-measures the real shipped Detector against the real shipped defaults
(read from a fresh processor's APVTS, not hand-copied constants) at
44.1/48/96 kHz and freezes (a) each band's |measured − default| within
1 dB (1.5 dB at 96 kHz), (b) the cross-band onset-gap spread within 1.5 dB —
the actual uniformity property, pinned directly — and (c) the
engages-at-anchor / idle-12-dB-below behaviour of the default-on demo band
at the design brief's own sourced starting Range.

### A.2 Control-law analysis: confirmed and frozen, not changed

Issue #4's "sensible defaults" scope includes where on the *knob's travel*
the musically useful zone sits. Analysis of the shipped control laws (all
values read from the real parameter objects):

- **Freq** (log, 20 Hz – 20 kHz): mid-travel is the geometric mean
  √(20·20000) = 632.5 Hz — within 0.4% of the default-on demo band's 630 Hz.
- **Attack** (log, 0.1 – 500 ms): mid-travel √(0.1·500) = 7.1 ms; the design
  brief's own sourced starting recipe (10 ms) sits at 0.54 travel.
- **Release** (log, 5 – 1500 ms): mid-travel √(5·1500) = 86.6 ms; the sourced
  ~100 ms recipe value sits at 0.53 travel.
- **Q** (0.4 skew, 0.3 – 12): mid-travel Q = 2.37, with the musically common
  0.7 – 4 window straddling the knob's centre (0.26 – 0.63 travel).
- Every per-band Q/Threshold/Attack/Release default sits within the middle
  half (0.25 – 0.75) of its knob's travel.

Conclusion: the existing ranges and skews already put the useful zone at the
centre of the knob — **no range or skew was changed** (deliberately: a
mapping change silently re-curves existing host automation lanes, the exact
hazard the v0.4.0 state-schema note names for the still-deferred Range/Q
range widening). What changed is that none of this was previously *pinned*:
`tests/ControlLawTests.cpp` now freezes the mid-travel values, the
recipe-at-mid-travel property, and the middle-half-of-travel invariant, so an
accidental future skew edit fails a test instead of silently bending every
automation curve.

### A.3 What was considered and rejected

- **Defaulting Band 1/Band 6's Type to Shelf** (their documented roles are
  shelf-shaped, and the v0.3.0 table labels them "(Low Shelf)"/"(High
  Shelf)"): rejected. Q is ignored in Shelf mode, so both outer bands would
  ship with a dead Q knob — a direct violation of this plugin's binding
  "readability of control state" design principle (a control that silently
  does nothing at default). The shelf roles remain one Type click away, and
  at default (Gain/Range 0) the choice is inaudible anyway.
- **Widening the Range/Q parameter ranges**: still deferred, unchanged —
  needs the state-schema-3 automation remap (see the v0.4.0 CHANGELOG note).

### A.4 Honesty section

- **The anchor is a proxy, not programme material.** Pink noise at -18 dBFS
  RMS is a defined, reproducible, convention-backed stand-in for "typical
  mix level and tilt", and the calibration replaces numbers that were never
  measured against *anything*. It does not claim per-genre optimality: real
  mixes deviate from pink (most fall off faster above ~5 kHz, many carry
  more low end), so on real material the high bands will in practice engage
  somewhat later than the low bands at the same programme loudness — a
  deviation that now at least has a defined zero point.
- **The issue's original "by-ear comparison against established dynamic
  EQs" was not performed** — same structural limitation the v0.3.0 honesty
  section (§3, below) already names: no access to the reference plugins for
  a direct A/B. This pass closes issue #4 with measured calibration as the
  validation mechanism; a human listening session against the reference
  class remains worthwhile for any future voicing revisit, and the per-band
  Attack/Release numbers (v0.3.0 §1.1) remain judgment-tuned.
- **The "engages gently" window in A.1's engine-level proof (mean gain
  reduction between 0.1 and 3 dB at the anchor)** is a deliberately wide
  acceptance band chosen to pin the qualitative behaviour (active, nowhere
  near the Range rail) without over-fitting the test to one noise
  realisation.

---

# v0.3.0 — voicing notes (historical)

Scope: issue #4, "musical defaults and character pass" (M2 milestone). This
document records what was changed for v0.3.0, what was *measured* (backed
by a Catch2 regression test that exercises real DSP behaviour, not just a
parameter value), and what is still by-ear engineering judgment rather than
sourced or measured - per this repo's standing convention (see
`docs/design-brief.md`'s own Honesty section for the v0.2.0 precedent this
document follows the same shape as).

## 1. What changed

### 1.1 Per-band default Q / Threshold / Attack / Release

Pre-v0.3.0 (v0.1.0 and v0.2.0), every band shared the exact same Q (1.0),
Threshold (-30 dB), Attack (5 ms), and Release (150 ms) default, varying
only in Freq. `docs/design-brief.md` §3 (v0.2.0) explicitly considered and
rejected changing these, on the grounds that no *research finding* singled
out a per-band difference - a defensible call for a research-sourced pass,
but it left the plugin's six bands behaving identically at rest regardless
of the very different real-world role each band's frequency implies (a
100 Hz band is a boom/resonance-control tool; a 4 kHz band is a sibilance/
harshness tool - these do not want the same ballistics).

v0.3.0 gives each band its own default, tuned to that role along the
existing frequency ladder (Freq defaults themselves are unchanged - see
§3.2 of the honesty section below for why):

| Band | Freq | Role | Q | Threshold | Attack | Release |
|---|---|---|---|---|---|---|
| 1 | 100 Hz (Low Shelf) | Boom/sub control | 0.9 | -26 dB | 25 ms | 280 ms |
| 2 | 250 Hz | Mud/box resonance (vocal & guitar body) | 1.1 | -28 dB | 15 ms | 180 ms |
| 3 | 630 Hz | General midrange presence (default-on demo band) | 1.0 | -26 dB | 8 ms | 130 ms |
| 4 | 1600 Hz | Vocal presence / guitar edge | 1.2 | -24 dB | 4 ms | 100 ms |
| 5 | 4000 Hz | Sibilance / pick attack / harshness | 1.4 | -22 dB | 2 ms | 70 ms |
| 6 | 10000 Hz (High Shelf) | Air / fizz recovery | 1.0 | -20 dB | 3 ms | 90 ms |

Reasoning: low-frequency boom/resonance control should move slowly - fast
ballistics on a 100 Hz band audibly pumps and modulates everything above it
(the classic "breathing bass" problem); high-frequency sibilance/harshness
control needs to be fast enough to catch a consonant or pick transient
before it's already over. Attack and Release step down roughly
monotonically from Band 1 to Band 5 for exactly this reason. Band 6 (the
air/fizz recovery shelf) sits close to Band 5, not at the slow end - a
shelf used for de-essing-style "air recovery" needs the same kind of
responsiveness a sibilance band does, not a slow tonal-balance ballistic.
Q increases slightly from Band 1 (0.9, a bit wider - broad boom control
shouldn't thin the low end) toward Band 5 (1.4, a bit narrower - more
surgical in the harshness/sibilance region, matching the classic "narrow,
high-Q band for resonance taming" mixing convention already documented in
this manual's own Tips section). Threshold moves progressively less
negative (engages a bit later) climbing the ladder, loosely following the
same "least amount of processing possible" philosophy the v0.2.0 design
brief cites from its seasoned-mixer source, applied per-band rather than
uniformly.

**Range stays 0 dB (idle) for every band, unchanged.** This repeats
`docs/design-brief.md`'s own deliberate v0.2.0 decision: a band must not
move until the user asks it to. None of the ballistics table above is
audible until a Range is dialed in - but once it is, each band now reacts
the way its documented role suggests it should, rather than every band
reacting identically regardless of what it's nominally for.

**Measured, not just asserted**: `tests/BallisticsDefaultsTests.cpp`
reads each band's *actual* shipped default Attack/Release straight out of
a freshly constructed processor's APVTS (not a hand-duplicated constant),
feeds those exact values into an isolated `Detector`, and measures a real
step-response ordering - proving the resulting envelope-follower behaviour
is genuinely ordered slowest (Band 1) to fastest (Band 5), not just that
the numeric defaults differ. `tests/ParameterTests.cpp` freezes the
per-band default *values* themselves as an ordinary parameter-layout
regression.

### 1.2 Gentle Saturation (`bN_sat`, new per-band boolean, off by default)

A new opt-in, off-by-default per-band toggle. When on, a soft
`std::tanh(x * drive) / drive` waveshaper (unity slope at the origin
regardless of drive - a small-signal-transparent, large-signal-soft-
clipping shape) is applied to that band's own post-filter samples, but
**only while the band is actively boosting** - `on == true` and the
combined (static + dynamic) applied gain for the current sub-block is
strictly positive. A cutting or idle band is completely unaffected even
with `sat` on. Drive scales from a low, near-transparent floor at 0 dB of
boost up to a clearly audible (but still soft-knee-shaped, not
hard-clipped) drive at +12 dB (this plugin's own Gain/Range ceiling):

```
fraction = clamp(appliedGainDb / 12, 0, 1)
drive    = lerp(fraction, 0.3, 2.5)
output   = tanh(input * drive) / drive
```

Implementation: `DynamicBand::computeSaturationDrive()` /
`DynamicBand::applySaturation()` (`src/dsp/DynamicBand.cpp`), gated inside
`processSubBlock()`'s existing "band is on and gain != 0" branch (the same
branch the exact-0-dB true-bypass optimisation already lives in - see
`docs/architecture.md`'s "exact-0-dB bypass" section). No allocation, no
lookahead, no added latency.

**Measured, not just asserted**: `tests/SaturationTests.cpp` drives an
isolated `DynamicBand` with Range = 0 (a pure static-gain band, so the
saturation stage is measured independent of the gain computer) and
estimates a best-fit linear gain by correlating output against input
(`a = <in,out> / <in,in>`); the RMS of the residual `output - a*input`,
normalised by the input's own RMS, is an FFT-free proxy for "how much
non-linear energy did this stage add." The tests prove: (a) a +9 dB boost
with Saturation on measures at least 5x the (near-zero) linear-stage
distortion baseline; (b) a -9 dB cut with Saturation on measures
indistinguishable from the same cut with Saturation off (the "boosted
bands only" scope, actually verified rather than just documented); (c) an
idle (0 dB Gain, 0 dB Range) band is bit-identical with Saturation on
(the exact-0-dB true-bypass path, unaffected); (d) output stays finite at
the +12 dB Gain ceiling.

### 1.3 New factory preset: "Analog Warmth Lift"

Band 2 (250 Hz, low-mid body): +2 dB static Gain, +3 dB upward Range
(boosts further as the signal gets loud), `bN_sat` on. A musically ordinary
"a bit of warmth on the body of the track" boost, chosen specifically to
demonstrate Saturation's new "gentle, boosted-bands-only" character on a
use case (console-style low-mid warmth) rather than an extreme/aggressive
one. See `docs/presets.md`.

## 2. What was NOT changed

- **Frequency defaults** (100/250/630/1600/4000/10000 Hz): unchanged.
  `docs/design-brief.md` §3 already established these bracket the
  documented problem-frequency territory (450-500 Hz mud, 2-3 kHz
  harshness) between existing bands; no new finding in this pass revisits
  that.
- **Gain/Range ranges and defaults** (±12 dB, 0 dB idle): unchanged, same
  reasoning as `docs/design-brief.md` §3 - a band must not move until asked
  to, and no concrete use case was identified needing headroom beyond
  ±12 dB.
- **Attack/Release *ranges*** (0.1-500 ms / 5-1500 ms): unchanged, only the
  per-band *defaults within* those ranges moved.
- **Auto Release / Gain-Q coupling mechanisms**: unchanged from v0.2.0 -
  this pass adds a new per-band default *starting point* and a new
  saturation toggle, it does not revisit the v0.2.0 ARC-inspired or
  gain/Q-coupling implementations themselves.

## 3. Honesty section

- **The per-band Q/Threshold/Attack/Release voicing table (§1.1) is this
  document's own engineering judgment, layered on top of the existing,
  already-documented frequency ladder** - it is not sourced from a
  specific reference plugin's per-band preset bank or factory patch (unlike
  several v0.2.0 numbers, which cited a specific Waves/Sonnox/TDR Nova
  documented value). The *direction* (low frequency = slow/gentle, high
  frequency = fast/surgical) is a well-established, uncontroversial mixing
  convention (also stated in this manual's own pre-existing Tips section:
  "narrow, high-Q bands... are the classic resonance-taming setup"), but
  the exact numbers (25 ms vs. 8 ms vs. 2 ms, -26 dB vs. -22 dB) are tuned
  engineering judgment, not measured against real vocal/guitar/mix
  material or calibrated against a competitor's shipped presets. This is
  the single most "by ear, not measured" claim in this pass - flagged here
  explicitly rather than described as "measured voicing," matching this
  repo's `docs/design-brief.md` precedent for how to talk about this kind
  of choice honestly.
- **What IS measured**: the *ordering* of the resulting ballistics (Band 1
  slower than Band 2 slower than Band 3... down to Band 5) is a real,
  Catch2-verified property of the shipped Detector step-response, not just
  a claim about the numbers on paper (see `tests/BallisticsDefaultsTests.cpp`).
  The *specific* numbers chosen to produce that ordering remain
  judgment-calibrated, not measured against source material.
- **The saturation drive curve (§1.2: floor 0.3, ceiling 2.5, linear
  fraction of Gain/12 dB) is this feature's own invention**, motivated by
  the general "gentle, non-fixed drive that scales with how hard the band
  is boosting" goal stated in issue #4, not sourced from any reference
  plugin's actual saturation stage (none of the four reference plugins
  cited in `docs/design-brief.md`/`docs/research-notes.md` document a
  saturation stage at all - this is a Lancet-original addition, not a
  reproduction of an existing "F6-class" feature). What IS measured is that
  the stage genuinely adds harmonic/non-linear energy when engaged while
  boosting (§1.2's distortion-ratio proof) and genuinely does NOT engage
  while cutting or idle (also measured, not just asserted from reading the
  code).
- **No by-ear listening comparison against Waves F6 / FabFilter Pro-Q /
  TDR Nova / Sonnox Oxford Dynamic EQ was performed for this pass** -
  same structural limitation as `docs/design-brief.md`'s own honesty
  section already names (this author has no ownership/access to those
  plugins for a direct A/B). Issue #4 asked for "by-ear comparison against
  established dynamic EQs" as part of its scope; this pass does not
  deliver that specific item, and it is named here as the clearest
  concrete gap for a future iteration, not silently dropped. A future pass
  with actual access to the reference plugins (or real mix material and a
  human listening session) should specifically A/B the per-band ballistics
  table (§1.1) and the saturation drive curve (§1.2) against real program
  material, since both are currently judgment-tuned rather than
  ear-validated.
- **The new factory preset's own numeric choices** (+2 dB static Gain,
  +3 dB Range on Band 2) are, likewise, engineering judgment chosen to
  produce an audible-but-gentle boost for demonstrating Saturation, not a
  sourced or measured value.

## 4. What remains for a later pass

- A real by-ear validation session against the reference class (see §3
  above) - the single clearest open item from issue #4's original scope.
- GUI controls for `bN_autoRelease`/`bN_gainQ`/`bN_sat` - all three remain
  automation/preset-only; dedicated editor toggles are roadmap M3 alongside
  the custom LookAndFeel pass (`docs/design-brief.md` §7, this repo's
  `CLAUDE.md`).
- If a future pass gains access to real vocal/guitar/mix reference
  material, the per-band Threshold defaults in particular are the
  softest-sourced numbers in §1.1's table (chosen mostly to spread evenly
  between -20 dB and -28 dB rather than from any specific measurement) and
  would benefit most from real-material validation.

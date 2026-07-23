# Lancet v0.3.0 — voicing notes

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

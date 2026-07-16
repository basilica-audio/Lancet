# Lancet — Design Brief v2 (draft)

Target version: **v0.2.0**. Pre-1.0: breaking parameter changes are allowed.
State migration policy: **tolerant import** — a v0.1.0 state tree missing v2
parameters loads with v2 defaults filled in for the new IDs; a v0.1.0 state
tree's existing parameter IDs are preserved as-is (no silent value
remapping), since none of the changes below rename or rescale an existing ID.

> Template note: this brief was asked to follow the structure of a sibling
> `miserere-design-brief-v2.md`, but no such file exists on this machine (see
> the same note independently reached in `silentium-brief-v2.md`,
> `overture-brief-v2.md`, etc.). This document follows the six-section shape
> given directly in the task description: why v1 falls short → topology →
> module specs with sourced defaults → test guarantees → honesty →
> versioning.

## 1. Why v1 falls short

Lancet v1 (M1, v0.1.0) is a correctly engineered six-band dynamic EQ:
real-time-safe, well-tested (47/47 Catch2 tests green), with a defensible
core architecture — serial minimum-phase bells/shelves, per-band detection
tapped pre-chain and band-filtered so bands don't retrigger each other, a
soft-knee gain computer, 32-sample sub-block coefficient updates. But
measured against the reference class this brief researched — **Waves F6**
(the plugin that defines "F6-class"), **FabFilter Pro-Q 3/4**, **TDR Nova**,
**Sonnox Oxford Dynamic EQ** — it is an **"engineered core"**: the topology
is right, but several specific behaviours are generic where the category is
opinionated. Concretely (full sourcing in `lancet-research-notes.md`):

1. **Zero program-dependence in the ballistics.** v1's Attack/Release are
   fixed per-band ms values, full stop — the gain-computer ramp rate never
   varies with how the signal is actually moving. Both flagship references
   treat adaptive ballistics as their headline advanced feature: Waves F6's
   ARC "calculates the shortest possible release time based on the signal's
   envelope," and FabFilter states Pro-Q's dynamic EQ is "highly program
   dependent: attack, release and knee all depend on the processed audio...
   and the current dynamic range." This is the single largest authenticity
   gap in v1 — larger than any individual range or default number.
2. **The knee width is a flat, unsourced 6 dB constant.** Every reference
   plugin either ties knee to another control (TDR Nova: "implicit,
   ratio-dependent knee... smooth at low ratio, gradually sharpens as ratio
   increases") or documents a specific, deliberate number (Sonnox: fixed
   10 dB soft knee). A single flat 6 dB knee cannot serve both a subtle
   -2 dB "glue" move and an aggressive -10 dB de-essing cut equally well —
   it is the most obviously "engineered placeholder" number in the current
   spec.
3. **Attack floor is slower than the category's fast end.** v1's Attack
   floor is 0.5 ms; Sonnox's Attack knob reaches 0.10 ms. For fast transient
   work (drum-bus snare crack containment, de-essing onset) the reference
   class goes faster than v1 currently allows.
4. **Release ceiling and Attack ceiling don't reach the reference class's
   documented top end.** F6's Attack goes to 500 ms (5x v1's 100 ms
   ceiling) — used for slow, musical, non-transient-triggered moves (long
   program-dependent tonal balancing, not de-essing). v1's own ceiling
   silently forecloses that use case.
5. **Range/Threshold defaults are generic zero-state, not a documented
   "reasonable starting point."** A seasoned-mixer source (Waves) gives a
   concrete, quotable dynamic-EQ starting recipe: Range -6 dB, Attack 10 ms,
   Release ~100 ms, aimed at named problem frequencies (450-500 Hz mud,
   2-3 kHz harshness). v1's Threshold default of -30 dB and per-band
   frequency defaults (100/250/630/1.6k/4k/10k Hz) are self-consistent but
   were not checked against this documented starting-point pattern.
6. **No gain/Q coupling — the bands stay surgically fixed-width at any gain
   depth.** Sonnox documents deliberate "gain/Q dependency whereby the Q
   reduces with gain, providing the EQ with a softer characteristic
   resembling that of analog EQs." v1's Q is static regardless of how hard a
   band is moving — a real, sourced voicing difference between "surgical"
   and "musical" dynamic EQ character that v1 doesn't currently offer either
   way.
7. **De-essing — the category's single most common real-world use case — has
   no discoverable path in v1.** Every reference plugin's documentation and
   marketing treats de-essing as a primary application; the Waves forum's
   "How to set up the perfect de-esser" thread describes a specific
   two-band-stack pattern (fast-attack cutting node + high-shelf air
   recovery node) that v1's six generic bands can already execute
   mechanically, but nothing in the current spec surfaces it as an
   intended workflow (no preset, no naming hint).

## 2. Topology (v2)

```
in ── [Input Trim] ─┬─ Band 1 ─ Band 2 ─ Band 3 ─ Band 4 ─ Band 5 ─ Band 6 ─ [Mix] ─ [Output Trim] ── out
                    │   (serial minimum-phase IIR bells/shelves, gain per band =
                    │    static gain + dynamic gain from that band's detector,
                    │    band Q now optionally narrows/widens with |dynamic gain|)
                    └─ per-band sidechain: input tapped PRE-chain, band-filtered → envelope
                         │
                         └─ program-dependent release: envelope's own fall rate
                            (not just the fixed Release-ms constant) shortens the
                            *effective* release time when the envelope itself is
                            already falling fast (transient decay), same mechanism
                            direction as F6's ARC, opt-in per band
```

Everything from v1's topology is retained unchanged: serial bell/shelf
chain, pre-chain per-band detection, 32-sample sub-block coefficient
updates, zero-latency biquad-only signal path, Mix/Trim placement. Three
additions layer onto the existing `Detector`/`DynamicBand` split without
restructuring it:

- **Knee width becomes a function of Range, not a flat constant.** Same
  quadratic soft-knee shape (`softKneeOvershoot`, unchanged math), but the
  knee-width input is now derived from `|rangeDb|` instead of the current
  hardcoded 6 dB — following TDR Nova's documented "smooth at low
  ratio/depth, sharper at high ratio/depth" behaviour, translated to this
  plugin's Range-in-dB (not ratio) control per §3.
- **Program-dependent auto-release (opt-in per band).** A new
  boolean, `bN_autoRelease`, off by default (exact v1 ballistics reproduced when
  off — required for tolerant import and for guarantee #1's null test to
  keep holding). When on, the *effective* release time for a given
  transition is the minimum of the user's Release-ms setting and a
  computed value derived from the detector envelope's own recent fall
  rate — directly modeled on F6's documented ARC behaviour ("always...
  shorter than the Release setting"), not a replacement for the manual
  Release control.
- **Gain/Q coupling (opt-in per band).** A new boolean, `bN_gainQ`, off by
  default. When on, the band's effective Q at any instant is scaled down
  (widened) proportionally to `|dynamicGainDb| / |rangeDb|` — following
  Sonnox's documented "Q reduces with gain" analog-EQ-style softening —
  so a band sitting near its Range ceiling reads as a broader, gentler
  move than the same band at rest. Static Gain (non-dynamic) never affects
  Q; only the *dynamic* component does, since that's specifically what the
  reference documents.

## 3. Module specs — parameters, ranges, sourced defaults

Generic descriptors only (no brand/person names) — matches v1's existing
naming convention (`bN_*` APVTS IDs, per band b1..b6 unless noted).

### Frequency (`bN_freq`)
- Range unchanged: 20-20,000 Hz log, defaults unchanged:
  100/250/630/1.6k/4k/10k Hz.
- Reasoning: v1's spread already brackets the two most-cited problem bands
  from the seasoned-mixer source (450-500 Hz mud, 2-3 kHz harshness) between
  its existing 250 Hz/630 Hz and 1.6 kHz/4 kHz bands — no change needed,
  the six-band ladder already covers the documented territory.

### Q (`bN_q`)
- Range unchanged: 0.3-12, default unchanged: 1.0.
- Reasoning: the seasoned-mixer source's own mix-buss example used "Q ≈ 1.0
  at 2800 Hz" — directly matches v1's existing flat default. No numeric
  change; **gain/Q coupling is new and additive** (§2), applied on top of
  whatever static Q the user sets, not a replacement for it.

### Gain (`bN_gain`, static)
- Range unchanged: ±12 dB, default unchanged: 0 dB.
- No sourced reason to change; static Gain range was not a focus of any
  reference manual's dynamic-EQ-specific documentation (all four references
  document their *dynamic* ranges far more prominently than their *static*
  band gain range, which tends to match ordinary parametric EQ norms — v1's
  ±12 dB already sits in that ordinary-EQ norm).

### Range (`bN_range`, dynamic depth)
- Range unchanged: ±12 dB.
- Default **unchanged at 0 dB** (i.e., band starts static) — this is a
  correct, deliberate zero-state default (a band shouldn't move until the
  user asks it to), not a gap. What changes is the **documented preset
  starting point** (§5), not the parameter's own idle default: the
  seasoned-mixer source's "-6 dB" recommendation is a *preset* value for an
  already-engaged band, not evidence the idle default should move off 0.
- v1's ±12 dB ceiling sits below F6's ±18 dB. **Left unchanged** — no
  found evidence that Lancet's use cases (heavy-mix surgical correction,
  per the plugin's stated positioning) need headroom beyond ±12 dB, and
  widening a dB range is a low-value, high-risk change absent a concrete
  driving use case. Flagged, not acted on (honesty section).

### Threshold (`bN_thresh`)
- Range unchanged: −60-0 dB, default unchanged: −30 dB.
- Reasoning: matches F6's documented Threshold range (−60 dB to 0 dB)
  exactly already — no change needed. Default of −30 dB sits at the
  range's midpoint, a defensible "moderate engagement" starting point
  consistent with the seasoned-mixer source's overall "least amount of
  processing possible" philosophy (a band that engages too early on every
  loud passage would contradict that philosophy).

### Attack (`bN_attack`)
- Range **widened to 0.1-500 ms** (floor lowered from 0.5 ms, ceiling
  raised from 100 ms), default unchanged: 5 ms.
- Reasoning: floor — Sonnox's Attack knob reaches 0.10 ms, faster than v1's
  0.5 ms floor; lowering matches the fastest documented reference exactly.
  Ceiling — F6's Attack range extends to 500 ms, used for slow tonal moves
  rather than transient catching; v1's 100 ms ceiling forecloses that
  entire slow-moving use case. Default unchanged: the seasoned-mixer
  source's own general-purpose starting point is "10 ms," close enough to
  v1's existing 5 ms default (both "fast," neither transient-instant) that
  no default change is warranted absent a stronger signal.

### Release (`bN_release`)
- Range **widened to 5-1500 ms** (floor lowered from 10 ms to 5 ms, ceiling
  raised from 1000 ms to 1500 ms), default unchanged: 150 ms.
- Reasoning: floor — Sonnox's Release knob reaches 5 ms, faster than v1's
  10 ms floor. Ceiling — F6's ARC explicitly computes releases *shorter
  than* the user Release setting on transient material, implying the user
  Release ceiling itself is meant to comfortably exceed "musical" values for
  slow, sustained-material use (mastering-style glue); Sonnox's own ceiling
  is 1 sec (1000 ms). 1500 ms is a modest, evidence-adjacent (not
  evidence-exact) extension beyond Sonnox's 1000 ms ceiling to give F6-style
  ARC (§2) genuine headroom to shorten *from* — flagged as the least
  strongly sourced individual number in this brief (honesty section).
  Default unchanged: the seasoned-mixer source's own starting point is
  "~100 ms," close to v1's existing 150 ms default — no change warranted.

### Knee width (internal, was a flat 6 dB constant — becomes derived)
- **New behaviour, not a new user-facing parameter.** Knee width in dB is
  now computed as `clamp(|rangeDb| * 0.5, 2.0, 10.0)` — i.e. scales with
  how deep the band's dynamic Range is set, floored at 2 dB (still audibly
  soft even for the smallest engaged Range). NOTE the effective ceiling:
  with this plugin's ±12 dB Range limit the formula tops out at
  12 × 0.5 = **6 dB** — the 10 dB clamp is deliberately unreachable
  headroom, NOT a target. This is intentional: full-depth moves on this
  plugin are still meant to read as more "surgical/decisive" than the
  softest analog-modeled knees of the reference class (which document a
  fixed 10 dB knee), consistent with Lancet's F6-class positioning.
  Test guarantee #2 asserts the 6 dB cap.
- Reasoning: directly implements the TDR Nova-documented principle ("smooth
  at low [depth], sharper at high [depth]") using this plugin's existing
  Range-in-dB control as the driving input instead of adding a Ratio
  control — preserves v1's topology decision (§ Research notes §5:
  Range-in-dB, not ratio, is the correct topology for an F6-class
  positioning) while still capturing the *musical result* Nova's
  ratio-coupled knee produces.
- v1's flat 6 dB is the special case at exactly Range = ±12 dB under the old
  code, and is reproduced by the new formula at Range = ±12 dB too
  (`12 * 0.5 = 6`), except the new formula also varies at every other Range
  value where v1's old flat constant did not — meaning full-depth-Range
  behaviour is **bit-compatible in shape** with v1, while shallower moves
  now differ (softer knee than before). This is a deliberate, documented
  behavioural change and cannot be made bit-identical at all Range values;
  see honesty section.

### Program-dependent release toggle (`bN_autoRelease`, new)
- Bool, default **off** for all bands.
- Reasoning: off-by-default is required for the tolerant-import guarantee
  (a v0.1.0 session with fixed Release-ms ballistics must sound identical
  after import — see §4 test #1) and matches the category norm that ARC-
  style behaviour is an *additive, optional* refinement layered on a
  working manual Release control, not a replacement for it (F6 itself keeps
  a manual Release *and* an ARC toggle, they coexist rather than ARC
  replacing manual release outright).

### Gain/Q coupling toggle (`bN_gainQ`, new)
- Bool, default **off** for all bands.
- Reasoning: same off-by-default logic as `bN_autoRelease` — a static-Q band is
  v1's existing, tested, bit-transparent-when-idle behaviour; Sonnox's
  documented gain/Q coupling is a *voicing choice* (analog-style softening),
  not a universal correctness fix, so it should be an opt-in character
  switch rather than a forced behavioural change to every existing band.

### `bN_on`, `bN_type`, `bN_listen`, `in_trim`, `out_trim`, `mix`
- All unchanged from v1. No reference-class finding touches these; they are
  architectural/workflow controls (band bypass, shelf-vs-bell, solo-listen,
  global trim/blend) rather than category-voicing parameters, and none of
  the four reference sources documented anything that would change them.

## 4. Test guarantees (Catch2, v0.2.0)

All existing v1 test categories are retained (null test, static-reference
correctness, dynamic-behaviour approach-to-Range, detector out-of-band
selectivity, NaN/Inf sweep, oversized-block clamp, state round-trip,
`reset()` state-clear, zero-latency, automation-smoothness zipper guard).
New/changed guarantees for v0.2.0:

1. **New-parameter null test**: with `bN_autoRelease = off` and `bN_gainQ = off`
   (the v0.2.0 defaults) on every band, a full test signal through v0.2.0
   must reproduce v0.1.0's measured output to the same ≤ −120 dBFS tolerance
   already used by guarantee #1 in the v1 brief — proves both new toggles
   are truly inert at their defaults, which is required for tolerant import
   to be meaningful rather than nominal.
2. **Knee-width curve proof**: for a fixed (freq, Q, Threshold) triple, sweep
   Range across at least three values (e.g. ±2 dB, ±6 dB, ±12 dB) and
   measure the actual gain-vs-overshoot curve around Threshold for each;
   assert the measured knee width (the overshoot span over which the curve
   deviates from both the flat-zero and the full-magnitude asymptotes)
   scales with Range per the `clamp(|range|*0.5, 2, 10)` formula within a
   defined tolerance, and that the ±12 dB case's curve matches v1's old
   fixed-6 dB knee shape — a real measured-curve proof, not just "the
   formula compiles."
3. **Program-dependent release proof**: with `bN_autoRelease = on`, feed a signal
   that overshoots Threshold briefly (short transient, envelope already
   falling fast when it crosses back under Threshold) versus a signal that
   overshoots for a sustained duration (envelope still near peak when it
   crosses back under); assert the measured time-to-settle-back-to-static-
   gain is shorter for the transient case than for the sustained case, and
   that both are ≤ the plain fixed-Release-ms case measured with `bN_autoRelease =
   off` on the same signal (never *slower* than manual Release, matching
   F6's documented "always shorter than the Release setting" claim) — this
   is the single most important new test, proving the auto-release toggle actually
   changes gain-computer *behaviour*, not just that a boolean flag exists.
4. **Gain/Q coupling proof**: with `bN_gainQ = on` and Range set to a
   nonzero value, drive the band to (a) near-zero dynamic gain (signal at
   rest, under Threshold) and (b) near-full dynamic gain (signal held well
   above Threshold, dynamic gain approaching Range); measure the band's
   actual −3 dB bandwidth in each state and assert bandwidth (b) is wider
   than bandwidth (a) by a measurable, non-trivial margin — proves Q is
   actually being modulated by dynamic gain, not just that the parameter
   is wired but numerically inert.
5. **Attack/Release range-boundary tests**: Attack = 0.1 ms and Attack =
   500 ms both produce finite, correctly-clamped coefficient updates (no
   NaN/Inf, no assertion failure) and measurably different envelope-
   follower time constants from each other and from the v1 boundary values
   (0.5 ms/100 ms) they replace; same pattern for Release at 5 ms/1500 ms.
6. **Tolerant state import test**: a serialized v0.1.0
   `AudioProcessorValueTreeState` XML (all current v0.1.0 `bN_*` IDs, none
   of the v2-new `bN_autoRelease`/`bN_gainQ` IDs) loads into v0.2.0 without error,
   with the two new per-band IDs populated at their v0.2.0 defaults (off)
   and all pre-existing IDs' values preserved exactly, including any
   pre-existing Attack/Release values that now sit well inside the widened
   ranges (no clamping needed for values that were already valid under the
   narrower v1 range).
7. **De-essing preset spectral proof** (see §5): process an identical
   sibilant-vowel test signal (synthesized or corpus fixture, high-frequency
   energy concentrated 4-9 kHz) through the "De-Ess Stack" preset and assert
   measurable sibilance-band energy reduction during the sibilant segments
   relative to the same signal with the preset's dynamic bands disabled —
   a real measured-spectrum proof that the shipped preset actually performs
   the documented function, not just that it loads.

## 5. Factory Presets (proposed, for the M2 preset system)

Generic names, no brand/person references. Intent-first naming, matching
the workflow-lore findings directly (§1.5, §1.7, research notes §7).

1. **Gentle Glue** — Band 2 (250 Hz-ish region) and Band 4 (2-3 kHz-ish
   region) engaged: Threshold −30 dB (default), Range −4 dB, Attack 10 ms,
   Release 100 ms, `bN_autoRelease` off, Knee auto (per §3 formula, ≈4 dB at this
   Range). Intent: the seasoned-mixer source's own general starting-point
   recipe (Range −6 dB / Attack 10 ms / Release ~100 ms) applied to the two
   named problem bands (mud, harshness), toned slightly gentler (−4 dB not
   −6 dB) per the same source's "least amount of EQ possible" philosophy.
2. **De-Ess Stack** — Two adjacent bands both centered near the sibilance
   region (e.g. Band 5 at ~6.5 kHz, Band 6 High-Shelf at ~9 kHz): Band 5
   fast-attack cutting node (Attack 0.5 ms, Release 60 ms, Range −8 dB,
   `bN_autoRelease` on); Band 6 slower recovery/air shelf (Attack 5 ms, Range +1.5
   dB boost, gentle). Intent: directly implements the Waves forum's
   documented two-node de-essing pattern (fast cutting node + high-shelf air
   recovery) using generic bands and naming.
3. **Transient Snare Crack** — Band 3 at ~2.5-3.5 kHz: Threshold −24 dB,
   Range +6 dB (upward/boost direction), Attack 0.1 ms (new floor), Release
   40 ms, `bN_autoRelease` on, `bN_gainQ` off (keep it surgically narrow, not
   softened). Intent: exercises the boost-direction Range use case
   (documented in research notes §7 — "enhancing snare attack only when
   hits trigger the threshold") and the new fast Attack floor.
4. **Mix Buss Settle** — Band 3 at 2.8 kHz, Q ≈ 1.0 (matches the
   seasoned-mixer source's own mix-buss example exactly): Threshold −20 dB,
   Range −3 dB, Attack 15 ms, Release 250 ms, `bN_gainQ` on (soft, analog-
   style widening as it engages). Intent: gentle, glue-not-correct mix-buss
   harshness control, directly sourced to a named practitioner example.
5. **Slow Tonal Ride** — Band 2, wide Q (0.5): Threshold −35 dB, Range −5
   dB, Attack 350 ms (near the new 500 ms ceiling), Release 800 ms.
   Intent: exercises the new slow end of the Attack/Release range —
   a musical, non-transient-reactive tonal balance move rather than a
   corrective one, the use case the widened F6-matched ceiling exists for.
6. **Chest Resonance Tamer** — Band 1 Low Shelf ~200-250 Hz: Threshold
   −28 dB, Range −5 dB, Attack 20 ms, Release 180 ms, Knee auto (soft at
   this shallow Range, ≈2.5 dB per formula floor). Intent: a common vocal/
   guitar low-mid boxiness use case adjacent to the documented 450-500 Hz
   mud band, using the shelf mode already present in v1.
7. **Fast-Recovery Demo** — Band 4: Threshold −25 dB, Range −8 dB,
   Attack 3 ms, Release 300 ms, `bN_autoRelease` **on**. Intent: not a mixing
   preset but a diagnostic/onboarding preset that makes the new auto-release
   toggle's audible difference discoverable — loading it and toggling
   `bN_autoRelease` on transient vs. sustained program material should be audibly
   different, per test guarantee #3.
8. **Listen Check** — `bN_listen` enabled on Band 1, all other parameters
   at v0.2.0 defaults. Intent: unchanged concept from v1's existing Listen
   feature, surfaced as a first preset so the detection-monitoring workflow
   is discoverable without reading the manual (same rationale used for
   Silentium's own "Listen Check" preset).

## 6. Honesty section

- **All defaults and ranges above are research-derived from software
  manuals, help documentation, and one practitioner tutorial/forum thread —
  none are from hardware measurement, plugin ownership, or listening
  comparison against F6/Pro-Q/Nova/Sonnox.** This category has no hardware
  anchor at all (dynamic EQ is a DAW-native, digital-only invention, unlike
  the suite's analog-modeled siblings) — that is a genuine, structural
  property of the reference class, not a gap in this research pass, and is
  named here explicitly rather than apologized for.
- **The primary Waves F6 PDF manual did not extract as readable text** via
  WebFetch (binary/stream-encoded PDF), and two mirror URLs 403'd or hit a
  TLS certificate error. All F6-specific numbers in this brief (Threshold
  −60-0 dB, Attack 0.5-500 ms, Range ±18 dB, the ARC quote) come from
  WebSearch's own aggregation of the same official content indexed
  elsewhere (waves.com product page + third-party summaries) — treated as
  **secondary-but-official-sourced**, not a direct primary-document fetch.
  Full detail in `lancet-research-notes.md` §8 (Fetch notes).
- **The Sonnox Oxford Dynamic EQ numbers (Attack 0.10-250 ms, Release
  5 ms-1 sec, 10 dB knee, gain/Q dependency) come from WebSearch aggregation
  of Sound on Sound's review and Sonnox's product copy, not a direct fetch
  of the Sonnox manual itself** (the manual is HTML and reachable at
  `dload.sonnoxplugins.com/pub/plugins/UserGuides/Oxford_Dynamic_EQ_User_Guide.html`
  — not fetched this pass under the ~8-12 page cost-discipline budget).
  A future iteration should fetch it directly before final implementation
  sign-off, particularly to confirm the exact gain/Q coupling formula shape
  (this brief's `bN_gainQ` implementation, §2-3, is this author's plausible
  mechanism inspired by the documented *goal*, not a reproduction of
  Sonnox's actual algorithm, which is proprietary and unpublished).
- **The knee-width formula (`clamp(|range|*0.5, 2, 10)`) is this brief's own
  invention**, not a number found in any source. It is *motivated* by TDR
  Nova's documented ratio-coupled-knee *principle* and calibrated so v1's
  existing full-Range behaviour is reproduced at Range = ±12 dB — but the
  slope (0.5), floor (2 dB), and ceiling (10 dB, chosen to reference
  Sonnox's fixed number without exceeding it) are engineering judgment
  applied to a sourced *principle*, not a sourced *number*. This is the
  brief's second-riskiest change after ARC and should be tuned by ear
  during implementation, with test guarantee #2 as a regression backstop
  rather than a correctness oracle.
- **The program-dependent release (`bN_autoRelease`) is the riskiest change in this
  brief**, same category of risk as Silentium v2's program-dependent gain
  ramp: F6's ARC is a Waves trade-secret algorithm with no public
  implementation detail beyond "calculates the shortest possible release
  time based on the signal's envelope." This brief's mechanism (effective
  release = min(user Release-ms, a value derived from the detector
  envelope's own recent fall rate)) is *inspired by* that one-sentence
  description, not a reproduction of it, and should be validated against
  real playing during implementation, not just against the Catch2 proof in
  §4.
- **The Release ceiling extension to 1500 ms is the single least strongly
  sourced individual number in this brief** — no reference plugin's
  documented ceiling reaches exactly 1500 ms (Sonnox's is 1000 ms; F6's
  isn't numerically published for Release the way it is for Attack). It was
  chosen to give the new ARC toggle genuine headroom to shorten *from*, not
  because any source states 1500 ms specifically. If a future direct fetch
  of the F6 manual PDF succeeds and contradicts this, the ceiling should be
  revisited before v0.2.0 ships.
- **Range's ±12 dB ceiling was explicitly considered for widening toward
  F6's ±18 dB and rejected**, on the grounds that no research finding
  identified a concrete use case Lancet currently can't serve at ±12 dB —
  named here so the decision is visible and revisitable, not silently
  dropped.

## 7. Versioning

- Target: **v0.2.0**.
- Pre-1.0 breaking changes allowed and used here: Attack range widened
  (0.5-100 ms → 0.1-500 ms), Release range widened (10-1000 ms →
  5-1500 ms), two new per-band boolean parameters (`bN_autoRelease`, `bN_gainQ`),
  changed (non-parameter-visible) internal knee-width computation.
- **State migration = tolerant import**: a v0.1.0
  `AudioProcessorValueTreeState` loads into v0.2.0 with all pre-existing
  parameter IDs preserved exactly (including any Attack/Release values that
  now sit inside a wider range — no clamping needed since the v1 range was
  a strict subset of the v2 range on both Attack and Release) and the two
  new v0.2.0-only IDs per band (`bN_autoRelease`, `bN_gainQ`) filled with their
  v0.2.0 defaults (both off). No parameter ID is renamed or rescaled by
  this brief, so no value-remapping logic is required beyond JUCE's own
  tolerant-XML-attribute handling — covered by test guarantee #6 in §4.
- No GUI changes are in scope here (M3, per the plugin's own CLAUDE.md — v0.1
  ships the standard slider editor, custom photoreal LookAndFeel is
  roadmap M3). No preset *system* implementation is in scope here either —
  §5 is content for M2's preset system to consume, not a spec for how
  presets are stored/browsed.

# Lancet deep-dive — research notes (dynamic EQ reference class)

Scope: F6-class dynamic EQ (per-band threshold/range/attack/release bells over
a serial static EQ). Sources below are primary manuals/docs where fetchable,
secondary (reviews, forum, brand tutorial) otherwise noted as such.

## Reference class selected

1. **Waves F6 Floating-Band Dynamic EQ** — the plugin the category name ("F6-class")
   comes from; six floating bands + HP/LP, per-band threshold/range/attack/release,
   internal or external sidechain, ARC (Automatic Release Control).
2. **FabFilter Pro-Q 3/4** — dynamic EQ mode on any of up to 24 bands; the
   "program-dependent auto ballistics, no user attack/release" design philosophy —
   the opposite pole from F6's fully manual ballistics, useful as a contrast case.
3. **TDR Nova** — free, four dynamic bands + W-band, ratio-based (not threshold+range)
   gain computer, implicit ratio-dependent knee. Good source for an alternate
   gain-computer topology and for widely-replicated workflow lore (it's the most
   commonly cited "how dynamic EQ works" teaching tool because it's free).
4. **Sonnox Oxford Dynamic EQ** — Depth (0–100 %) instead of dB Range, gain/Q
   dependency (Q narrows as gain increases, analog-EQ character), explicit 10 dB
   soft-knee spec, alternate "Onset" (transient) trigger mode besides level threshold.

## 1. Threshold / Range (gain-computer topology)

**Waves F6** (secondary source, AI-summarized from user guide since the PDF itself
would not parse as text — see Fetch Notes below):
- Threshold: **−60 dB to 0 dB**
- Attack: **0.5 ms to 500 ms**
- Range: **−18 dB to +18 dB**, "negative values are used for compression and
  positive values for expansion"
- ARC (Automatic Release Control): "calculates the shortest possible release
  time based on the signal's envelope, and this value will always be shorter
  than the Release setting for the band."
- Source: https://www.waves.com/plugins/f6-floating-band-dynamic-eq (AI
  summary of official user guide content aggregated from search index; the
  PDF at https://assets.wavescdn.com/pdf/plugins/f6.pdf did not extract as
  text via WebFetch — see Fetch Notes)

**FabFilter Pro-Q 3/4** (fetched, https://www.fabfilter.com/help/pro-q/using/dynamic-eq):
> "The dynamic behavior of Pro-Q 3 has been carefully tuned and is highly
> program dependent: attack, release and knee all depend on the processed
> audio, the frequency range of the EQ band and the current dynamic range."
- Dynamic Range ring: **−30 dB to +30 dB** (constrained further by the plugin's
  max gain limit)
- When collapsed (auto mode): "the attack and release are automatically set
  and threshold is constantly adjusting to the level of the current,
  band-limited trigger signal" — i.e. Pro-Q doesn't use a fixed dB threshold
  at all in its default mode; threshold self-adjusts to program level.
- Manual override exists, "with the center position (50%) matching auto-mode
  behavior" — so there IS a nominal manual attack/release/knee value FabFilter
  considers "average," it's just not published as a number.

**TDR Nova** (fetched, https://docs.tokyodawn.net/nova-manual/):
- Uses **Ratio** (0.5:1 expansive → 1:1 unity → ∞:1 compressive), not a
  Range-in-dB. "NOVA has an implicit, ratio dependent 'knee'. The knee is
  particularly smooth at low ratio and gradually sharpens as ratio increases."
- Gain: ±12 dB per band (same as our static Gain)

**Sonnox Oxford Dynamic EQ** (secondary, WebSearch-aggregated from
soundonsound.com / official product docs):
- "The Threshold control incorporates a **10 dB soft knee** so that the
  transition from Offset Gain to Target gain remains smooth."
- Depth ("Dynamics") knob: **0–100 %**, described as "a kind of compression
  ratio or depth control" rather than a dB Range.
- "The plug-in features **gain/Q dependency** whereby the Q reduces with
  gain, providing the EQ with a softer characteristic resembling that of
  analog EQs." — this is a well-documented, deliberate, non-obvious design
  choice: as a band's dynamic gain grows, its Q gets wider (not narrower),
  because "the Q reduces with gain" (lower Q number = wider bandwidth in
  their convention) — softening the curve at higher gain rather than keeping
  a fixed, surgically narrow notch/bump at all gain amounts.
- Alternate trigger: "Onset (transient) detection feature, for precise
  transient processing without the need for time-consuming automation" —
  i.e. a non-level trigger mode exists in the category (out of scope for
  Lancet v0.2 but worth naming in the brief as a considered/rejected option).

## 2. Attack / Release ballistics

- **F6**: Attack 0.5–500 ms; ARC computes release dynamically, shorter than
  the user Release setting, "based on the signal's envelope."
- **Sonnox Oxford Dynamic EQ** (fetched via WebSearch summary,
  https://www.soundonsound.com/reviews/sonnox-oxford-dynamic-eq and official
  product page): "The Attack knob (**0.10 ms to 250 ms**) sets how fast/slow
  the band reaches Target Gain, and Release (**5 ms to 1 sec**) determines how
  slowly the band recovers to the Offset Gain setting."
- **Pro-Q**: no user-facing numeric attack/release at all in default mode —
  algorithmic, program-dependent, varies "per frequency range of the EQ band."

Takeaway: our v1 ranges (Attack 0.5–100 ms, Release 10–1000 ms) sit *inside*
the F6/Sonnox envelope on both ends — F6 goes to 500 ms attack (5× our max)
and Sonnox's release floor is 5 ms (2× faster than our 10 ms floor). Neither
reference plugin caps attack as low as F6's floor of 0.5ms only — Sonnox goes
faster, to 0.1 ms, useful for pure transient snap on drum bands.

## 3. Knee

- **v1 Lancet**: fixed 6 dB knee width, `softKneeOvershoot()` — quadratic
  soft-knee identical in structure to a classic compressor gain computer,
  but the width is not user-adjustable and is not sourced from any reference
  plugin; it was an engineering placeholder.
- **Sonnox**: explicit fixed **10 dB** soft knee (their number, not user
  adjustable either — same category of choice we made, but bigger).
- **TDR Nova**: knee width is *derived from ratio*, not a separate parameter —
  smooth at low ratio, sharp at high ratio. This is the more "authentic"
  behavior for a musical dynamic EQ: gentle moves (small Range) should also
  read as gentle transitions; aggressive moves (large Range, i.e. de-essing/
  transient snap) should read as a harder onset. A single fixed knee width
  cannot serve both a −2 dB "glue" band and a −12 dB de-esser band well.
- **F6**: no explicit published knee-width number found in the sources that
  did fetch; Waves documents ARC (release-side program dependence) far more
  prominently than knee-side program dependence, suggesting F6's knee is
  fixed/simple and the "musicality" burden is carried by ARC instead.

Gap: v1's 6 dB flat knee is the single most "generic engineered" choice
in the design — every reference plugin either ties knee to another control
(Nova: ratio) or documents a specific number as a deliberate voicing choice
(Sonnox: 10 dB). Six is neither.

## 4. Detector / sidechain filter design

- v1 Lancet: **cascaded 2× RBJ bandpass** matched to band freq/Q, peak
  envelope, pre-chain tap. This is architecturally close to F6's "floating
  band" detection (each band's dynamics react to *that band's own frequency
  content*, not broadband level) — this part of v1 is already well-aligned
  with the reference class and does not need to change.
- F6 adds **external sidechain** and **M/S mode** per band (both explicitly
  out-of-scope for Lancet M1, correctly deferred — no v2 change needed here,
  just confirm the deferral still holds).
- Sonnox adds an **Onset/transient** trigger alternative to level threshold —
  worth a one-line "considered, deferred" note for architectural honesty but
  not for v0.2 scope (would need a differentiator/transient detector, a
  different subsystem).

## 5. Ratio/depth vs. Range-in-dB — topology choice validation

Two competing gain-computer topologies exist in the reference class:
- **Range-in-dB, threshold-triggered** (F6, Pro-Q): overshoot above threshold
  is scaled and clamped to a dB ceiling. This is what v1 Lancet already does
  (`bN_range` ±12 dB clamp). **Keep this** — it's the dominant, most legible
  topology and matches the plugin's F6-class positioning directly.
- **Ratio-based** (Nova) or **Depth-percentage** (Sonnox): overshoot is scaled
  by a ratio or percentage with no hard dB ceiling (Nova) or a normalized 0–100%
  knob (Sonnox). These read as more "compressor-native" to engineers coming
  from dynamics processors rather than EQs.
Decision for v2: **do not switch topology** — Range-in-dB is correct for an
"F6-class" positioning and switching would be a category mismatch, not an
authenticity improvement. But the *default depth values* and *knee-width
coupling* should still be re-tuned per findings above.

## 6. Program-dependence as a category-defining feature

Both flagship references (F6's ARC, Pro-Q's fully-automatic ballistics) treat
**adaptive release** as the single most emphasized "advanced" feature of a
modern dynamic EQ — more emphasized than knee, more emphasized than exact
threshold range. Quote (Waves, aggregated): ARC "calculates the shortest
possible release time based on the signal's envelope." Quote (FabFilter,
fetched): "attack, release and knee all depend on the processed audio... and
the current dynamic range."

Gap: v1 has **zero program-dependence** — attack/release are fixed
per-band ms values, full stop. This is the single largest authenticity gap
versus the reference class, larger than the knee-width issue. A full
Pro-Q-style automatic algorithm is out of scope for a v0.2 EQ-scoped patch,
but an F6-style **optional program-dependent release assist** (opt-in,
per-band or global toggle, not replacing the manual Release knob but
capping/shortening it when the envelope is very transient) is a
proportionate v2 addition that speaks directly to what both reference
plugins consider their headline dynamics feature.

## 7. Workflow lore / default starting points (secondary sources, WebFetch-aggregated)

- De-essing (Waves forum thread, "How to set up the perfect de-esser",
  https://forum.waves.com/t/waves-f6-how-to-set-up-the-perfect-de-esser/9704):
  two stacked bell nodes at the same sibilance frequency — first node
  "fastest attack," "low to medium" range; second node "medium to high
  attack," deeper cut. Optional wide high-shelf recovery: "Q of around 0.5"
  at "around 2000 or so Hz," "a dB or two" boost, to restore air removed by
  the de-essing cut.
- General dynamic-EQ starting point (Waves "Dynamic EQ Tips from a Seasoned
  Mixer", https://www.waves.com/dynamic-eq-tips-from-seasoned-mixer):
  Range **−6 dB**, Attack **10 ms**, Release **~100 ms** as an initial
  "reasonable, not aggressive" starting point; problem frequencies named:
  **450–500 Hz** ("mud/cloudiness") and **2–3 kHz** ("harshness"); a mix-buss
  glue example used **Q ≈ 1.0 at 2800 Hz**. Direct quote: "Using a
  traditional EQ is often a compromise, because it is a static solution to
  what is often a dynamic problem." And: prefers "the least amount of EQ
  possible," dynamic EQ only "to help elements play together a little
  nicer" — i.e. subtle defaults, not aggressive ones, are the professional
  default mindset for this whole category.
- Dynamic EQ isn't purely subtractive: "enhancing snare attack only when
  hits trigger the threshold" — a **boost (upward)** direction use case,
  confirming v1's existing bidirectional Range design (±12 dB, sign of
  Range determines boost-when-loud vs cut-when-loud) is correctly aligned
  with real usage, not just a symmetric engineering nicety.

## 8. Fetch notes / source-quality caveats

- The primary Waves F6 PDF (assets.wavescdn.com/pdf/plugins/f6.pdf) and two
  mirrors (promusic.cz, cdn-docs.av-iq.com) did **not** extract as readable
  text via WebFetch (binary/stream-encoded PDF, or TLS cert error). F6 numbers
  above (threshold, attack, range, ARC quote) come from WebSearch's own
  aggregation of the same official user-guide content indexed elsewhere
  (waves.com product page + third-party summaries), not a direct manual
  fetch. Treated as **secondary-but-official-sourced** rather than primary —
  flagged explicitly in the brief's honesty section.
- manualmachine.com and manualzz.com (both F6 manual mirrors) returned
  HTTP 403 to WebFetch — not used.
- TDR Nova numbers are a mix of a direct fetch of the official Tokyo Dawn
  docs site (topology, ratio range, knee behavior — high confidence) and the
  Scribd manual excerpt (low value, excluded from citations).
- Sonnox Oxford Dynamic EQ numbers come from WebSearch aggregation of
  Sound on Sound's review and Sonnox's own product copy, not a direct manual
  fetch (the Sonnox manual is HTML at
  https://dload.sonnoxplugins.com/pub/plugins/UserGuides/Oxford_Dynamic_EQ_User_Guide.html,
  not fetched directly this pass — future deep-dive iteration could fetch it
  directly for exact param names).
- No hardware unit anchors this category (dynamic EQ is a DAW-native/digital
  invention, unlike Miserere/Overture-class analog-modeled units) — all
  sources above are software manuals/docs, which is itself a genuine,
  non-negotiable property of this category (documented explicitly in the
  brief's honesty section rather than treated as a gap).

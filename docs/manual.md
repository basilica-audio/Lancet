# Lancet — user manual

*Cut where it counts — a surgical dynamic EQ with an analog soul.*

## What's new in v0.4.0

**The Attack knob is now true down to 0.1 ms.** Up to v0.3.0 there was a
fixed 50 ms smoother sitting behind the gain computer, so no Attack setting
faster than about 50 ms could actually be heard - 0.1 ms, 1 ms and 20 ms all
did the same thing. That smoother is gone; the detector's own ballistics are
now the only thing shaping how fast a band moves, evaluated once per sample.

**Please read this if you have existing sessions.** A session using a non-zero
Range with a fast Attack *will* react faster than it did before. This is a bug
fix, not a re-voicing - the plugin now does what its own knob always said it
did - but it is a real, audible change and you should expect to hear it. If a
band now feels too grabby, dial its Attack up: for the first time, that
control has the effect its label describes. Sessions with `Range = 0` on every
band (pure static EQ, no dynamics) are unchanged, and the static EQ curves
themselves are identical to v0.3.0's in every band type, gain and Q.

Also new:

- **Sidechain (SC Source: Internal / External).** Each band can now take its
  detector input from an external sidechain instead of the audio passing
  through it - so a band can duck against a kick, a lead vocal, or anything
  else your host can route. Enable the plugin's sidechain input in your host
  first; if there is no sidechain available, a band set to External simply
  keeps using the internal signal rather than going quiet.
- **SC Mode: Split / Wide.** Split (the default, and the only behaviour
  previous versions had) means the band only listens to its own frequency
  region. Wide means it listens to the *whole* signal while still only
  moving its own band - the usual way to make one band pump with the mix.
  Listen follows whichever you pick, so you always hear what is really
  triggering the band.
- **Cleaner Saturation.** The per-band Saturation stage now uses an
  anti-aliased waveshaper. Same character, noticeably less of the gritty
  fold-back harmonics that a plain waveshaper produces on high-frequency
  content - and still no added latency.
- **Smoother Gain/Q.** With Gain/Q coupling on, the band's Q now glides with
  the dynamic gain instead of stepping.
- **An eleventh factory preset, Sidechain Carve**, demonstrating the new
  routing.

Both new per-band controls sit in the editor as combo boxes under each band's
Type slot. Sessions saved by older versions load exactly as before, with both
new controls at the settings that reproduce the old behaviour.

## What's new in v0.3.0

A musical-voicing pass (see `docs/voicing-notes.md`) - measured where the
plugin's own DSP made a measurement possible, honestly labeled "by ear, not
yet tuned against real material" where it doesn't:

- **Per-band Q/Threshold/Attack/Release defaults, tuned to each band's
  documented role** (was a flat Q 1.0/Threshold -30 dB/Attack 5 ms/
  Release 150 ms for every band): Band 1 (100 Hz, boom/sub control) now
  starts slow and gentle (Attack 25 ms/Release 280 ms); Band 5 (4 kHz,
  sibilance/harshness) starts fast (Attack 2 ms/Release 70 ms); the bands
  in between step down progressively. Range still starts at 0 dB (idle) for
  every band regardless - nothing moves until you dial in a Range, but once
  you do, each band now reacts the way its role suggests it should. See the
  updated per-band table below.
- **Gentle Saturation** (`bN_sat`, new per-band toggle, off by default):
  when on, a soft `tanh`-based waveshaper is applied to a band, but *only*
  while it's actively boosting (Gain + Range's dynamic contribution net
  positive) - a cutting or idle band is completely unaffected even with
  Saturation on. Automation/preset controllable in v0.3.0; a dedicated
  editor toggle is roadmap M3, same as Auto Release/Gain-Q.
- **A tenth factory preset, "Analog Warmth Lift"** (`docs/presets.md`):
  demonstrates the new Saturation toggle on a gentle low-mid boost.
- A v0.2.0 session loads cleanly into v0.3.0 (tolerant import): every
  existing parameter value is preserved exactly, and the new per-band
  Saturation toggle populates at its off default.

## What's new in v0.2.0

A research-derived deep-dive rework (see `docs/design-brief.md`/
`docs/research-notes.md`), the suite's M2 preset system, and a German frame
localisation:

- **Program-dependent Auto Release** (`bN_autoRelease`, new per-band toggle,
  off by default): when on, a band's *effective* release time shortens
  automatically - never slower than the manual Release setting - when the
  signal's own envelope is already falling on its own (e.g. a naturally
  decaying transient), inspired by (not a reproduction of) the "ARC"-style
  release behaviour documented in F6-class dynamic EQs. Automation/preset
  controllable in v0.2.0; a dedicated editor toggle is roadmap M3.
- **Gain/Q coupling** (`bN_gainQ`, new per-band toggle, off by default):
  when on, a band's own filter Q softens (widens) proportionally to how
  hard its dynamic gain is currently moving, for a gentler, more
  analog-style character at deeper dynamic moves - the band's *static*
  Gain never affects Q, only its dynamic component does. Same
  automation/preset-only status as Auto Release in v0.2.0.
- **Attack/Release ranges widened**: Attack now 0.1-500 ms (was 0.5-100 ms),
  Release now 5-1500 ms (was 10-1000 ms) - both ends, reaching both faster
  transient-catching and slower, musical tonal-balancing use cases.
- **Knee width is now derived from Range**, not a flat 6 dB constant -
  shallower Range settings now read as gentler/softer, full-depth (±12 dB)
  Range settings sound identical to v0.1.0's fixed 6 dB knee.
- **Eleven factory presets** (`docs/presets.md`) covering common use cases
  (glue, de-essing, transient enhancement, mix-buss settling, slow tonal
  balancing, resonance taming, and a diagnostic Auto Release demo), plus a
  preset bar (Save/Save As/Delete/Import/Export, factory + user library) at
  the top of the editor.
- A v0.1.0 session loads cleanly into v0.2.0 (tolerant import): every
  existing parameter value is preserved exactly, and the two new per-band
  toggles populate at their off default.

## What it is

Lancet is a six-band dynamic EQ in the spirit of the Waves F6 class - cited here as a documented reference point for the category, without implying endorsement, sponsorship, or affiliation by Waves Audio Ltd. Each band is a normal parametric EQ band (bell, or shelf on Band 1/Band 6) whose gain can additionally move with the program material. Feed it loud and it reacts — cutting a resonance only when it flares up, or opening a boost only when a part gets buried — then settles back to its static setting once the signal drops back down. Because each band's dynamic move is driven by its *own* pre-EQ, band-filtered detector, one band's cut never confuses another band's detector, and a band's own gain move never feeds back into its own detection.

Where a static EQ band asks "how much?", Lancet's dynamic bands also ask "when?" — the difference between permanently notching out a 3 kHz resonance (which also thins the tone whenever that resonance isn't present) and only pulling it back exactly when it rings.

## Where it sits in a mix chain

Lancet is a **corrective, surgical tool**, most useful early-to-mid signal chain, before broad tonal shaping and bus compression:

```
Source track -> [gain staging / gate] -> Lancet (resonance/harshness control) -> broad EQ / saturation -> compression -> bus
```

Reach for it when a static EQ cut would either under-treat the problem (leaving room for it to still poke through on the loudest hits) or over-treat it (thinning the tone on quieter passages where the problem isn't present). It also works as a mix-bus or master-bus tool for controlling a specific recurring resonance or harshness band without permanently coloring everything under it.

## Signal flow

```
in --[Input Trim]--+--[pre-chain tap]--> each band's Detector (bandpass @ band freq/Q -> envelope)
                    |
                    +--> Band1 -> Band2 -> Band3 -> Band4 -> Band5 -> Band6 --> [Mix] --> [Output Trim] --> out
```

Every band's detector taps the signal right after Input Trim, *before* Band 1 - not that band's own serially-processed input - so a downstream band's gain move never perturbs an upstream band's detection, and no band's own move feeds back into triggering itself. See [`docs/architecture.md`](architecture.md) for the full engineering breakdown (gain-computer formula, detector selectivity, sub-block coefficient smoothing, Listen).

## Under the hood

### The per-sample gain path and the TPT SVF core

Up to v0.3.0 a band's realised gain came from a 50 ms `juce::SmoothedValue`
sitting *after* the gain computer - the detector could react in half a
millisecond and the filter would still take 50 ms to catch up, so most of
the shipped 0.1-500 ms Attack range did nothing audible. v0.4.0 deletes that
smoother and evaluates the whole chain - detector envelope, dB conversion,
soft-knee gain computer, Range clamp, filter gain - once per sample. The
detector envelope *is* the smoother now, and there is no stepped-gain
fallback for any band type.

Deleting the smoother from a coefficient-rebuilding biquad would have been a
zipper-noise generator on its own, so the filter core changed too. A
direct-form biquad whose coefficients jump every sample is not a
well-defined time-varying filter - its internal state means something
different after each jump. Every band (bell, low shelf, high shelf) instead
runs on `lnct::TptSvf`, a topology-preserving-transform (trapezoidal-
integration) state-variable filter built from Andrew Simper's published
Cytomic equations, with no third-party code vendored. Its two state
variables are physical integrator outputs that keep their meaning no matter
how the coefficients move, so gain can be modulated every sample without
artefact. The bell's frequency warp is completely gain-independent -
dynamic gain enters only as a scalar bandpass mix, never by re-tuning the
centre frequency - and integrator state is kept in `double` even though the
audio path is `float`, because the trapezoidal update's own cancellation
loses digits in single precision at low frequency and high Q.

The realised static response is unchanged by any of this: verified against
an independent double-precision reference to better than -100 dBFS peak
residual over ten seconds of broadband noise per setting, and against the
analytic RBJ magnitude response to within ±0.05 dB across a band-type/gain/Q
grid. At exactly 0 dB every gain-dependent mix scalar is *exactly* zero, so
an idle band is bit-transparent by construction, with no branch and no
special case.

### Detector design

In Split mode, the detector cascades **two** bandpass biquads at the band's
own frequency/Q rather than one, because a single stage only reaches about
-12 dB two octaves out at Q 1 - not enough headroom against a loud
out-of-band tone falsely triggering the band. The cascade measures better
than -24 dB, comfortably past the plugin's own >20 dB two-octave isolation
bar. Stereo (or wider) input is **linked**: each cascade stage runs
independently per channel with its own filter state, but the envelope
follower is a single band-wide value fed by the loudest (max-abs) sample
across channels at every instant - which avoids the stereo-image shift that
fully independent per-channel gain reduction would introduce. There is no
per-band unlink option. The envelope itself is a one-pole peak follower run
**per sample** for correct ballistics timing; only the bandpass's own
coefficients are throttled to sub-block granularity, never the envelope.

### Auto-release: a dedicated fast reference envelope

Auto Release (off by default) shortens a band's effective release whenever
the signal's own envelope is already falling on its own, clamped so the
result is always at least as fast as - never slower than - the manual
Release setting. The detail worth knowing: the fall-rate measurement comes
from a **second, dedicated, always-fast envelope** inside the detector
(same Attack coefficient, but a fixed release tied to the plugin's own 5 ms
floor), not from the main envelope. Deriving it from the main envelope does
not work - a slow envelope is itself a low-passed view of the input,
rate-limited to roughly its own time constant, so "how fast is the slow
envelope falling" mostly measures the slow envelope's own coefficient back
at itself. An earlier internal implementation attempt made exactly that
mistake, and its output was measurably identical whether Auto Release was
on or off.

### Anti-aliased saturation, still at zero latency

The optional per-band Saturation stage replaces a plain `tanh` waveshaper
with a first-order antiderivative-antialiased (ADAA1) kernel of the same
shape, computed as the difference quotient of `ln(cosh(x))` between
consecutive samples - same harmonic character, measurably less fold-back,
no oversampling stage and no added latency. The specification is
deliberately relative rather than an absolute alias-floor claim; see
"Latency and aliasing" below for the measured numbers and the reasoning.
Saturation only ever engages while a band is actively boosting (static +
dynamic gain net positive) - a cutting or idle band is bit-identical with it
switched on.

### Engineering hygiene

- **Zero heap allocations on the audio thread**, proven under a replaced
  allocator, including with the sidechain bus active and Auto Release /
  Gain-Q / Saturation engaged on every band. Coefficient updates write a
  stack-only `ArrayCoefficients` result directly into already-allocated
  storage instead of calling `juce::dsp::IIR::Coefficients::makeXxx()`,
  which heap-allocates a fresh object on every call.
- **Idle work is skipped.** The detector bandpass and the SVF frequency
  warp recompute only when the smoothed frequency/Q have actually moved
  past an epsilon, and the per-sample SVF scalars are memoised on an exact
  gain match, so a band sitting at a settled gain recomputes nothing and is
  bit-transparent once its parameters are static.
- **NaN/Inf recovery is designed in, including on the sidechain.** A
  non-finite value would otherwise poison the SVF's integrator recursion
  permanently; the filter snaps its state clean and returns 0, recovering
  within the same block rather than needing a `reset()` - verified with a
  NaN/Inf-poisoned sidechain and all six bands set to External, which
  cannot silence or poison the main output.

## Parameter reference

### Per band (Band 1 - Band 6, identical controls unless noted)

| Parameter | Range | Default | Unit | What it does musically |
|---|---|---|---|---|
| **On** | Off / On | Off (Band 3: On) | | Enables the band. An off band is a true bypass - it doesn't touch the signal at all, though its detector keeps running underneath so there's no jump when you switch it back on. |
| **Type** | Bell / Shelf | Bell | | **Band 1 and Band 6 only.** Band 1's Shelf is a Low Shelf (boosts/cuts everything below Freq); Band 6's Shelf is a High Shelf (boosts/cuts everything above Freq). Bands 2-5 are always Bell. |
| **Freq** | 20 - 20000 | 100 / 250 / 630 / 1600 / 4000 / 10000 | Hz | The band's centre frequency (Bell) or corner frequency (Shelf) - both the filter's own shape *and* what its detector listens to. |
| **Q** | 0.3 - 12 | 0.9 / 1.1 / 1.0 / 1.2 / 1.4 / 1.0 (v0.3.0, per band - see table below) | | How narrow (high Q) or broad (low Q) the band is. **Ignored in Shelf mode**, which always uses a fixed, standard shelf slope (Q = 0.707) regardless of this setting. |
| **Gain** | -12 - +12 | 0 | dB | The band's *static* gain - always applied, dynamic or not. Set this to your "at rest" EQ move; Range then adds or subtracts on top of it when the detector triggers. |
| **Range** | -12 - +12 | 0 | dB | How far the band's gain can move dynamically, on top of Gain. **0 = a pure static EQ band** (no detector influence at all). Negative Range cuts as the signal gets louder past Threshold (the classic resonance-taming/de-essing move); positive Range boosts as it gets louder (an upward "duck-in" expansion move, useful for e.g. bringing out a pick attack only on hard-hit notes). |
| **Thresh** | -60 - 0 | -24 / -25 / -24 / -24 / -24 / -24 (calibrated, per band - see table below) | dB | The detector level above which the dynamic move starts engaging. A soft knee centred on this value makes the transition in gradual rather than a hard switch - the knee's own width scales with Range (v0.2.0): `clamp(\|Range\| * 0.5, 2, 10)` dB, so shallow Range settings read gentler and full-depth (±12 dB) Range settings sound identical to v0.1.0's fixed 6 dB knee. |
| **Attack** | 0.1 - 500 | 25 / 15 / 8 / 4 / 2 / 3 (v0.3.0, per band - see table below) | ms | How quickly the dynamic gain moves once the detector crosses Threshold. Fast attack catches transients hard; slower attack lets a brief peak through before reacting, which can sound more natural on percussive material. The 500 ms ceiling is meant for slow, musical tonal-balancing moves, not transient catching. **Since v0.4.0 this control is true across its whole range** - see "What's new in v0.4.0" above; before that, everything below ~50 ms behaved identically. |
| **Release** | 5 - 1500 | 280 / 180 / 130 / 100 / 70 / 90 (v0.3.0, per band - see table below) | ms | How quickly the dynamic gain returns toward Gain once the detector drops back below Threshold. Fast release can pump audibly on sustained material; slow release smooths the return out but can hold a cut/boost into content that no longer needs it. |
| **Listen** | Off / On | Off | | Solos that band's own detector signal - the bandpass-filtered, pre-EQ audio that's actually driving its dynamic move - in place of the normal program output, for auditioning exactly what triggers it. Exclusive: engaging Listen on one band disengages any other band's Listen. The full signal chain (including every band's own processing) keeps running underneath, so disengaging Listen never pops. |
| **Auto Release** (v0.2.0) | Off / On | Off | | Program-dependent auto-release: when on, the *effective* release time for a given transition shortens automatically (never below this plugin's own 5 ms Release floor, never past the manual Release setting itself) whenever the signal's own envelope is already falling on its own - useful for letting a band relax faster on naturally-decaying material without giving up a slower, musical manual Release for sustained material. Automation/preset-only in v0.2.0 - no dedicated editor knob yet (roadmap M3). |
| **Gain/Q** (v0.2.0) | Off / On | Off | | Gain/Q coupling: when on, the band's own filter Q widens (softens) proportionally to how far its *dynamic* gain currently sits toward Range - a gentler, more analog-style character at deeper dynamic moves. Static Gain never affects Q, only the dynamic component does. Automation/preset-only in v0.2.0 - no dedicated editor knob yet (roadmap M3). |
| **SC Source** (v0.4.0) | Internal / External | Internal | | Where this band's detector listens. **Internal** (the default, and every previous version's only behaviour) is the signal passing through the plugin, tapped before Band 1. **External** is the plugin's sidechain input - route something else to it in your host and this band moves in response to *that* instead, while still filtering the main signal. If your host provides no sidechain, or the sidechain input is left disabled, a band set to External falls back to Internal rather than going silent. No delay compensation is applied to the sidechain, so it must already be time-aligned by the host. |
| **SC Mode** (v0.4.0) | Split / Wide | Split | | How much of the detector's source this band listens to. **Split** (the default) filters the detector input down to this band's own frequency region, so only content near Freq can trigger it - the surgical behaviour. **Wide** skips that filter, so the band responds to overall level across the whole spectrum while still only moving its own band. Wide is what you want when a band should breathe with the mix rather than police one resonance. Listen follows this setting, so you always audition the real trigger signal. |
| **Saturation** (v0.3.0) | Off / On | Off | | Gentle waveshaping: when on, a soft drive is applied to the band's own output, but only while it's actively boosting (Gain + the dynamic contribution net positive) - a cutting or idle band is unaffected even with this on. Drive scales with how hard the band is boosting (barely-there near 0 dB, clearly audible but still soft-knee-shaped near +12 dB). Since v0.4.0 the waveshaper is anti-aliased, so it adds far less of the harsh fold-back grit that a plain waveshaper produces on high-frequency content, at no latency cost. Automation/preset-only - no dedicated editor knob yet (roadmap M3). |

Per-band voicing defaults (`docs/voicing-notes.md`) - Q/Attack/Release tuned
to each band's typical role along the existing frequency ladder (v0.3.0),
Threshold calibrated by measurement so that every band begins engaging at the
same programme loudness (issue #4 calibration pass - see below):

| Band | Freq | Role | Q | Threshold | Attack | Release |
|---|---|---|---|---|---|---|
| 1 | 100 Hz (Low Shelf) | Boom/sub control | 0.9 | -24 dB | 25 ms | 280 ms |
| 2 | 250 Hz | Mud/box resonance (vocal & guitar body) | 1.1 | -25 dB | 15 ms | 180 ms |
| 3 | 630 Hz | General midrange presence (default-on demo band) | 1.0 | -24 dB | 8 ms | 130 ms |
| 4 | 1600 Hz | Vocal presence / guitar edge | 1.2 | -24 dB | 4 ms | 100 ms |
| 5 | 4000 Hz | Sibilance / pick attack / harshness | 1.4 | -24 dB | 2 ms | 70 ms |
| 6 | 10000 Hz (High Shelf) | Air / fizz recovery | 1.0 | -24 dB | 3 ms | 90 ms |

Each band's default Threshold equals that band's own measured detector level
under a -18 dBFS RMS pink-noise programme anchor (the common digital
alignment-level convention), so at typical mix level every band sits right at
the edge of engaging: dial in a Range and the band immediately starts working
on anything at or above normal programme loudness, on every band alike. Run
material hotter or colder than that convention (or into an Input Trim
adjustment) and the engagement point shifts with it - Threshold remains an
ordinary, per-band control; only its *default* is calibrated.

### Global

| Parameter | Range | Default | Unit | What it does |
|---|---|---|---|---|
| **Input Trim** | -12 - +12 | 0 | dB | Gain applied before Band 1 - and before every band's detector taps the signal, so it also shifts what level reaches each band's Threshold. |
| **Output Trim** | -12 - +12 | 0 | dB | Gain applied after Band 6 and after the Mix blend - the final gain stage, for matching Lancet's output level to whatever follows it in the chain. |
| **Mix** | 0 - 100 | 100 | % | Parallel dry/wet blend of the whole six-band chain. 100% is fully processed; lower values blend in progressively more of the untouched (but still Input-Trimmed) signal - useful for "New York"-style parallel dynamic EQ, where you want the correction to add rather than fully replace. |

## Presets

A preset bar sits at the top of the editor: `[<] [Preset Name] [>]` to step
through the factory and user library alphabetically, `Save`/`Save As...` to
write your own, `Delete` for user presets, `Import.../Export...` for single
`.basilicapreset` files or `.zip` banks, and a menu (click the preset name)
with a "Set current as default" entry for your own out-of-the-box starting
point. Eleven factory presets ship - see `docs/presets.md` for
what each one does and why. User presets are stored per-user at
`~/Library/Audio/Presets/Basilica Audio/Lancet/` on macOS
(`%APPDATA%/Basilica Audio/Lancet/Presets/` on Windows).

The editor's frame strings (preset bar labels, menus, dialogs) are
localised to German automatically when the system language is German;
parameter names, units, and technical terms (Attack, Release, Hz, dB, ms, …)
always stay in English, matching every other Basilica Audio plugin.

## Tips

- **Set Gain and Range separately, deliberately.** Gain is what the band does *always*; Range is what it does *in addition*, only when triggered. A band with Gain=0, Range=-6 is silent at rest and only cuts when the resonance flares - very different from Gain=-3, Range=-3, which is always cutting a bit and cuts harder when triggered.
- **Use Listen to find the problem before you set Threshold.** Sweep Freq/Q with Listen engaged until you clearly hear the resonance or harshness in isolation, *then* set Threshold to just above where it sits when it's not a problem - this is far more reliable than guessing at a Threshold value against the full mix.
- **Narrow, high-Q bands with negative Range are the classic resonance-taming setup** (a boxy 300-500 Hz build-up, a harsh 2-4 kHz pick/reed edge, a sibilant 6-8 kHz de-esser band). Keep Q high enough that the cut doesn't audibly thin the surrounding tone when it engages.
- **Wide, low-Q bands with negative Range make a gentler, broader dynamic tone control** - useful on a mix bus for taming a whole register (e.g. "the low-mids get a bit much whenever the whole band hits together") without the surgical narrowness of a de-esser-style band.
- **Positive Range (upward/duck-in) is the less obvious move** - try it on a low-Q high-frequency band to bring out pick attack or breath/consonant detail only on the notes that need it, rather than boosting the whole register (and its noise floor) all the time.
- **Fast Attack + fast Release can pump audibly on sustained material** (bass, pads, sustained vocal notes) - if a band sounds unstable or "breathing," try a slower Release first before reaching for a narrower Q.
- **Mix below 100% keeps the dynamic move's character while reducing its depth** - a quick way to dial back an over-aggressive Range setting without re-tuning every band's Threshold/Range from scratch.
- **Reach for SC Mode: Wide when you want a band to breathe with the mix, not police one resonance** (v0.4.0). Split is the right default for surgical work - only content near Freq can trigger the band. Wide makes the band respond to overall level while still only moving its own frequency region, which is what you want for "duck the low-mids whenever the whole band hits" or for a band tracking a full-range sidechain source. A Wide band with a narrow Q is a perfectly sensible combination: the Q shapes *what moves*, the mode decides *what triggers it*.
- **With an external sidechain, set Threshold against the sidechain's level, not the main signal's** (v0.4.0). Once SC Source is External the band is reacting to whatever your host routes in, so the level reaching Threshold has nothing to do with what Lancet is inserted on. Engage that band's Listen to hear the sidechain feed directly while you find the Threshold - Listen always auditions whatever the detector is actually hearing, including the sidechain.
- **Time-align the sidechain in your host if it matters.** Lancet inserts no alignment delay anywhere (see "Latency and aliasing" below), so a sidechain that arrives late in your host arrives late at the detector too.
- **Now that Attack is true, try it before you reach for anything else** (v0.4.0). If a band grabs too hard on transients, a slower Attack is a real control again rather than a no-op below ~50 ms - that is usually a better first move than widening Q or backing off Range.
- **Saturation is only worth switching on where a band boosts.** It is scoped to boosting bands by design, so enabling it on a cutting de-esser band does nothing at all. Pair it with a modest positive Gain (and optionally a positive Range) on a body/warmth band.

## Latency and aliasing

**Lancet adds zero latency - always.** It reports 0 samples before and after
your host prepares it, at every sample rate and block size, with every band
engaged and with the sidechain bus enabled. That is verified by impulse
response (the peak really comes back on sample 0), not merely reported, and
there is no dry-path delay compensation anywhere in the plugin. Every filter
in the signal path - the six bands and their detectors - is minimum-phase
with no lookahead.

Two v0.4.0 decisions keep it that way, deliberately:

- **The Saturation stage is anti-aliased arithmetically rather than by
  oversampling.** It uses a first-order antiderivative-antialiased
  waveshaper of the same `tanh` shape the previous versions used, so it has
  the same harmonic character with measurably less fold-back - and no
  oversampling stage, which would have cost either latency or phase
  distortion. Measured suppression of the dominant 30 kHz to 18 kHz fold at
  48 kHz is 13.2 dB at -24 dBFS, 10.0 dB at -12 dBFS and 8.4 dB at -6 dBFS
  input. **No absolute alias floor is claimed**, and that is honest rather
  than coy: first-order treatment at the base sample rate does not earn one.
  If you drive a band hard on very high-frequency content and want a
  guaranteed absolute floor, that is what an oversampling plugin is for,
  and oversampling costs latency.
- **The external sidechain carries no alignment delay**, so it must already
  be time-aligned by the host - the same requirement comparable dynamic EQs
  have.

**Sample rates and formats.** AU, VST3 and Standalone. Verified finite and
zero-latency at 44.1, 48, 88.2, 96, 176.4 and 192 kHz, and across a
sample-rate change mid-session. Mono and stereo main layouts are both
supported; the sidechain input can be disabled, mono or stereo independently
of the main layout, and anything wider than stereo on it is rejected rather
than silently misinterpreted.

## Sessions, presets and compatibility

Sessions saved by any earlier version load cleanly, with every stored
parameter value preserved exactly and every parameter added since then
sitting at the default that reproduces the older behaviour. Concretely: a
v0.1.0 session's values survive, the v0.2.0/v0.3.0 per-band toggles (Auto
Release, Gain/Q, Saturation) come back off, and v0.4.0's twelve new per-band
choices come back at Internal and Split - the only routing any previous
version ever had.

Saved state carries a schema-version stamp from v0.4.0 onward. A state
without one is read as the older schema. Nothing needs converting today
(every newer parameter's default *is* the older behaviour), but the stamp
means a future release that genuinely does need to convert something has a
reliable way to tell what it is reading.

**One caveat, and it is the important one:** while your stored *values* are
untouched, a session that used a non-zero Range together with a fast Attack
will *sound* different under v0.4.0, because the Attack path was broken
before and is now fixed. See "What's new in v0.4.0" at the top of this
document. The same applies to the ten factory presets that predate v0.4.0 -
none of their stored values changed, but the ones whose names promise speed
(De-Ess Stack, Transient Snare Crack, Fast-Recovery Demo) now behave the way
their names always claimed.

## Known limitations

Stated plainly, because knowing them is more useful than not:

- **The GUI is a functional slider/toggle/combo editor.** The custom
  vector-drawn interface with per-band gain-reduction needles is a later
  milestone. The plugin already measures what those needles will show, and
  that measurement is verified against the gain reduction actually present
  in the output - but nothing displays it yet.
- **Auto Release, Gain/Q and Saturation have no editor controls.** All three
  are fully automatable and fully preset-controllable; dedicated toggles
  arrive with the GUI pass. SC Source and SC Mode are the deliberate
  exceptions, because a sidechain routing you cannot select from the editor
  is not usable at all.
- **Q is ignored in Shelf mode.** Band 1's Low Shelf and Band 6's High Shelf
  always use the standard flat shelf slope (Q 0.707), for both the audible
  filter *and* its matched detector bandpass.
- **No absolute aliasing floor is specified** for the Saturation stage - see
  "Latency and aliasing" above.
- **Detection is stereo-linked with no unlink**, and there is no per-band
  M/S or L/R placement - see "Detector design" under "Under the hood" above.
- **There is no ratio control, by design.** Above the knee the gain moves
  1:1 with how far the detector has overshot Threshold, until it reaches
  Range - which acts as a hard ceiling on the dynamic depth. Range is the
  "how far can this move" control; the knee (whose width scales with Range)
  provides the ramp-in.
- **The per-band voicing defaults are engineering judgment, not ear-tuned
  against reference material.** The *direction* of the v0.3.0 voicing table
  (low frequency slow and gentle, high frequency fast and surgical) is an
  established mixing convention, and the resulting ballistics *ordering* is
  measured and frozen by the test suite. The exact numbers, and the
  Saturation drive curve, are tuned judgment - not validated against real
  vocal/guitar/mix material and not calibrated against any other product.
  A by-ear pass against reference-class dynamic EQs on real programme
  material is a named, still-open item; see `docs/voicing-notes.md` for the
  full honesty section on exactly which numbers are which.
- **Not yet in the plugin, tracked for later releases:** opt-in spectral
  resonance suppression, per-band M/S and L/R placement with stereo unlink,
  optional HF de-cramping (deliberately deferred, because de-cramping
  changes the *static* response of existing sessions), wider Range/Q/Release
  ranges (which need their own migration story, since remapping a parameter
  range changes host automation-curve mapping), lookahead, per-band ratio,
  linear phase, more than six bands, and a spectrum-analyzer/EQ-curve
  display.
- **Lancet is pre-1.0.** Release binaries for macOS are
  Developer-ID-signed, notarised and stapled; Windows binaries are not yet
  Authenticode-signed. Breaking changes are possible until v1.0.0; installers
  are a later milestone. See `README.md`.

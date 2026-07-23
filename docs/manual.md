# Lancet — user manual

*Cut where it counts — a surgical dynamic EQ with an analog soul.*

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
- **Nine factory presets** (`docs/presets.md`) covering common use cases
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
| **Thresh** | -60 - 0 | -26 / -28 / -26 / -24 / -22 / -20 (v0.3.0, per band - see table below) | dB | The detector level above which the dynamic move starts engaging. A soft knee centred on this value makes the transition in gradual rather than a hard switch - the knee's own width scales with Range (v0.2.0): `clamp(\|Range\| * 0.5, 2, 10)` dB, so shallow Range settings read gentler and full-depth (±12 dB) Range settings sound identical to v0.1.0's fixed 6 dB knee. |
| **Attack** | 0.1 - 500 | 25 / 15 / 8 / 4 / 2 / 3 (v0.3.0, per band - see table below) | ms | How quickly the dynamic gain moves once the detector crosses Threshold. Fast attack catches transients hard; slower attack lets a brief peak through before reacting, which can sound more natural on percussive material. The 500 ms ceiling is meant for slow, musical tonal-balancing moves, not transient catching. |
| **Release** | 5 - 1500 | 280 / 180 / 130 / 100 / 70 / 90 (v0.3.0, per band - see table below) | ms | How quickly the dynamic gain returns toward Gain once the detector drops back below Threshold. Fast release can pump audibly on sustained material; slow release smooths the return out but can hold a cut/boost into content that no longer needs it. |
| **Listen** | Off / On | Off | | Solos that band's own detector signal - the bandpass-filtered, pre-EQ audio that's actually driving its dynamic move - in place of the normal program output, for auditioning exactly what triggers it. Exclusive: engaging Listen on one band disengages any other band's Listen. The full signal chain (including every band's own processing) keeps running underneath, so disengaging Listen never pops. |
| **Auto Release** (v0.2.0) | Off / On | Off | | Program-dependent auto-release: when on, the *effective* release time for a given transition shortens automatically (never below this plugin's own 5 ms Release floor, never past the manual Release setting itself) whenever the signal's own envelope is already falling on its own - useful for letting a band relax faster on naturally-decaying material without giving up a slower, musical manual Release for sustained material. Automation/preset-only in v0.2.0 - no dedicated editor knob yet (roadmap M3). |
| **Gain/Q** (v0.2.0) | Off / On | Off | | Gain/Q coupling: when on, the band's own filter Q widens (softens) proportionally to how far its *dynamic* gain currently sits toward Range - a gentler, more analog-style character at deeper dynamic moves. Static Gain never affects Q, only the dynamic component does. Automation/preset-only in v0.2.0 - no dedicated editor knob yet (roadmap M3). |
| **Saturation** (v0.3.0) | Off / On | Off | | Gentle waveshaping: when on, a soft `tanh`-based drive is applied to the band's own output, but only while it's actively boosting (Gain + the dynamic contribution net positive) - a cutting or idle band is unaffected even with this on. Drive scales with how hard the band is boosting (barely-there near 0 dB, clearly audible but still soft-knee-shaped near +12 dB). Automation/preset-only in v0.3.0 - no dedicated editor knob yet (roadmap M3). |

Per-band voicing defaults (v0.3.0, `docs/voicing-notes.md`) - tuned to each
band's typical role along the existing frequency ladder, not a flat value
repeated across every band:

| Band | Freq | Role | Q | Threshold | Attack | Release |
|---|---|---|---|---|---|---|
| 1 | 100 Hz (Low Shelf) | Boom/sub control | 0.9 | -26 dB | 25 ms | 280 ms |
| 2 | 250 Hz | Mud/box resonance (vocal & guitar body) | 1.1 | -28 dB | 15 ms | 180 ms |
| 3 | 630 Hz | General midrange presence (default-on demo band) | 1.0 | -26 dB | 8 ms | 130 ms |
| 4 | 1600 Hz | Vocal presence / guitar edge | 1.2 | -24 dB | 4 ms | 100 ms |
| 5 | 4000 Hz | Sibilance / pick attack / harshness | 1.4 | -22 dB | 2 ms | 70 ms |
| 6 | 10000 Hz (High Shelf) | Air / fizz recovery | 1.0 | -20 dB | 3 ms | 90 ms |

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
point. Ten factory presets ship with v0.3.0 - see `docs/presets.md` for
what each one does and why. User presets are stored per-user at
`~/Library/Audio/Presets/Yves Vogl/Lancet/` on macOS
(`%APPDATA%/Yves Vogl/Lancet/Presets/` on Windows).

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

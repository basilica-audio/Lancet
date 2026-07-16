# Factory presets

Nine factory presets ship with Lancet v0.2.0, embedded via BinaryData from
`presets/factory/*.json` (see `docs/design-brief.md` §5 for the sourced
rationale and `docs/research-notes.md` for the underlying reference-class
research). All are research-derived starting points, not measured-hardware
or listening-comparison calibrated - see `docs/design-brief.md`'s Honesty
section for exactly what that means.

| Preset | Category | Intent |
|---|---|---|
| **Default** | Init | The plugin's own out-of-the-box defaults (Band 3 on at a static, non-dynamic Bell; every other band off), exposed as an explicit preset so there's always a one-click way back to it. Also this plugin's M2 default-resolution starting state. |
| **Gentle Glue** | Bus | Band 2 (mud region) and Band 4 (harshness region, retuned to 2.5 kHz for this preset) both engaged with the seasoned-mixer source's own general starting-point recipe (Attack 10 ms / Release ~100 ms), toned slightly gentler (Range -4 dB, not -6 dB) per that same source's "least amount of EQ possible" philosophy. |
| **De-Ess Stack** | Vocals | The Waves-forum-documented two-node de-essing pattern: Band 5 is a fast-attack cutting node (0.5 ms attack, auto-release on) at the sibilance region; Band 6 is a wide (Q 0.5), gentle High Shelf "air recovery" boost restoring the top end the cut removes. |
| **Transient Snare Crack** | Drums | Boost-direction (upward) Range on Band 3, fast Attack (0.1 ms, the new floor) and auto-release on - exercises the "enhance a hit only when it triggers" use case documented in the research notes, keeping the band surgically narrow (Gain/Q coupling off). |
| **Mix Buss Settle** | Bus | Band 3 at 2.8 kHz / Q 1.0 - matches the seasoned-mixer source's own mix-buss glue example exactly - with Gain/Q coupling on for an analog-style softening as the band engages. |
| **Slow Tonal Ride** | Master | Band 2, wide Q (0.5), Attack near the new 500 ms ceiling and a correspondingly slow 800 ms Release - a musical, non-transient-reactive tonal balance move exercising the widened slow end of the Attack/Release range. |
| **Chest Resonance Tamer** | Vocals | Band 1 Low Shelf at 220 Hz - a common vocal/guitar low-mid boxiness use case adjacent to the documented 450-500 Hz mud band, using the shelf mode already present in v0.1. |
| **Fast-Recovery Demo** | FX | Diagnostic/onboarding preset (Band 4, auto-release on) - not a mixing preset, but a one-click way to A/B `bN_autoRelease` on transient vs. sustained program material and hear the difference the new toggle makes. |
| **Listen Check** | Init | `bN_listen` enabled on Band 1, everything else at plain defaults - surfaces the detection-monitoring workflow as a discoverable first preset, same rationale as v0.1's own Listen feature. |

The two new v0.2.0 per-band booleans (`bN_autoRelease`, `bN_gainQ`) are
automation/preset-controllable in v0.2.0 but have no dedicated editor
control yet - GUI work for them is deliberately deferred to M3 alongside the
rest of the custom LookAndFeel pass (see `docs/design-brief.md` §7 and this
repo's `CLAUDE.md`).

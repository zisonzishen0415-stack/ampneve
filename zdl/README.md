# AmpNeve ZDL (Zoom MultiStomp)

Custom AMP-category effect for the Zoom G1on / MS-series (ZDL). Original DSP:
soft clip (Drive) -> Neve transformer even harmonics + 1073-style EQ (Neve)
-> cabinet voicing, dark..bright (Tone) -> Level. First release exposes
4 knobs; Cab and Bass body are fixed internally (cab=1.0, bass=0.5).

| Knob | Param | Range | Maps to |
|---|---|---|---|
| 1 | Drive | 0..1 | soft-clip drive |
| 2 | Tone | 0..1 | cabinet dark..bright |
| 3 | Neve | 0..1 | Neve coloration amount |
| 4 | Level | 0..1 | output (0.5..1.5 gain) |

## Hardware / build status

- The core is ZDL-safe (no heap, no double, no math library, no division;
  all filter coefficients are precomputed constants).
- State lives in the host-managed `ctx[3]` arena (one Ampsim instance per
  channel, ~776 bytes total).
- NOT yet hardware-tested. Before loading on a pedal, follow the project's
  hardware-probe practice (research_docs/docs_SAFE-DSP-RULES.md in the
  reverson repo): start with an `audio_nop` smoke build, verify it appears,
  then enable the DSP.

## Build requirements

- TI C6000 Code Generation Tools (cl6x, e.g. ti-cgt-c6000 8.5.0 LTS).
- Python 3.
- The ZoomMultistompZDL toolchain (linker.py). `build.py` looks for it at
  `../zoomreverse/ZoomMultistompZDL` or `AMPNEVE_ZDL_ROOT`.

## Build

```sh
export AMPNEVE_TI_ROOT=/path/to/ti-cgt-c6000_8.5.0.LTS
python3 build.py
# -> ../dist/AmpNeve.ZDL
```

Load with Zoom Effect Manager (point it at the `dist/` folder), per
reverson/research_docs/docs_INSTALLING-ZDLS.md.

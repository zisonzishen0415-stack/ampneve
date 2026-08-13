# AmpNeve ZDL (Zoom MultiStomp)

Custom AMP-category effect for the Zoom G1on / MS-series (ZDL). Original
boutique-amp DSP: input stage -> gain stage with touch dynamics -> tone
network -> power stage with sag -> Neve transformer + 1073-style EQ ->
speaker resonance + cabinet -> level. Six knobs across two LineSel pages
(mirrors the VST):

| Page | Knob | Param | Range | Maps to |
|---|---|---|---|---|
| P1 | 1 | Gain | 0..1 | preamp gain + touch-dynamics base |
| P1 | 2 | Bass | 0..1 | tone low (0.5 flat) |
| P1 | 3 | Mid | 0..1 | tone mid (0.5 flat) |
| P2 | 1 | Treble | 0..1 | tone high (0.5 flat) |
| P2 | 2 | Master | 0..1 | power drive + sag amount |
| P2 | 3 | Level | 0..1 | output (0.5..1.5 gain) |

Cab voicing blend and Neve coloration are fixed internally (brand sound).

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

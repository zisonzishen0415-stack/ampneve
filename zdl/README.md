# AmpNeve ZDL (Zoom MultiStomp)

Custom AMP-category effect for the Zoom G1on / MS-series (ZDL). Original
boutique-amp DSP: input trim (fixed) -> input stage -> gain stage with
touch dynamics -> tone network -> power stage with sag -> Neve transformer
+ 1073-style EQ -> speaker resonance + cabinet voicing + 1024-tap cabinet
IR convolution (miked-cab kernel: mic + cone resonances + room) -> level.
Nine knobs + a Voice switch across the LineSel pages (mirrors the VST).
Voice toggles Nashville (session sheen) vs Emo/Edge (earlier breakup,
more mid body, warmer top):

| Page | Knob | Param | Range | Maps to |
|---|---|---|---|---|
| P1 | 1 | Bass | 0..1 | tone low (0.5 flat) |
| P1 | 2 | Mid | 0..1 | tone mid (0.5 flat) |
| P1 | 3 | Treble | 0..1 | tone high (0.5 flat) |
| P2 | 1 | Gain | 0..1 | preamp gain + touch-dynamics base |
| P2 | 2 | Master | 0..1 | power drive + sag amount |
| P2 | 3 | Level | 0..1 | output (0.5..1.5 gain) |
| P3 | 1 | Neve | 0..1 | coloration wet/dry (0 = bypass) |
| P3 | 2 | Cab | 0..1 | cabinet dark(0)..bright(1) |
| P3 | 3 | Presence | 0..1 | speaker 3.5 kHz resonance |
| V  | 1 | Voice | 0/1 | switch: 0 = Nashville, 1 = Emo/Edge |

Input trim is fixed at 1.0 (the calibrated reference) and takes no knob:
the pedal's hardware INPUT VOL does the level matching before the DSP,
exactly like plugging into a tube amp.

## Hardware / build status

- The core is ZDL-safe (no heap, no double, no math library, no division;
  all filter coefficients are precomputed constants).
- State lives in the host-managed `ctx[3]` arena (one Ampsim instance per
  channel, ~9.8 KB of DSP state total; 1248 floats reserved each - most of
  it the IR's rolling delay buffer, the kernel itself is static const).
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

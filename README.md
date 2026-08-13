# AmpNeve - amp + cabinet + Neve-style coloration

A from-scratch, pedal-safe guitar amp simulator, built as an **independent
effect** (separate from the Reverson reverse reverb). It turns a DI /
preamp guitar take into a "through a clean amp + 1x12 cab + Neve console"
sound, and ships as a **VST3** plus a **Zoom G1on / MS-series ZDL**.

Original DSP - no extracted factory algorithms (boutique amp v1):

```
in -> input trim             level matching for any DI / loop take [Input]
   -> input stage            light asymmetric saturation, DC block
   -> gain stage             touch dynamics: clip drive rides the input
                              envelope (soft picks stay clean, hard picks
                              break up)                     [Gain]
   -> tone network           interacting bass/mid/treble peaking
                              (fixed centers, linear-gain coeffs) [Bass/Mid/Treble]
   -> power stage            softer saturation + sag (envelope-driven
                              gain dip on transients)       [Master]
   -> transformer            Neve-style even harmonics + 1073-style EQ
                              + DC block (fixed brand color)
   -> speaker                cabinet resonance (105 Hz + 3.5 kHz) + cab
                              dark..bright voicing (fixed blend)
   -> level                                                  [Level]
```

The Neve coloration is placed **before** the cab by design: the cab's high
rolloff tames the saturation harmonics so they never sound harsh (A/B'd
against post-cab placement in the reverson demo work).

## Why a separate project

The amp/cabinet is a signal-chain stage, not a reverb module. Keeping it
independent means:
- it can sit before Reverson (or any reverb/delay) in the pedal chain or DAW;
- the VST and ZDL package separately;
- the DSP stays tiny (612 bytes of state) and trivially ZDL-safe.

## Features

- **ZDL-safe core** (`core/ampsim.c`): no heap, no `double`, no
  `sinf/cosf/powf/logf`, no division, no large writable statics. All filter
  coefficients are precomputed constants (`core/ampsim_coeffs.h`, generated
  by `tools/gen_coeffs.py`), so the audio path is pure multiply-add.
- **Nashville session voice**: tight low end (105 Hz resonance +2.5 dB),
  forward ~850 Hz mid, glassy top (full 4th-order 10/12 kHz lowpass) -
  the clean/edge platform studio country and modern shoegaze players use.
- **10 parameters**: three pages x 3 (P1 Bass/Mid/Treble, P2 Gain/Master/Level,
  P3 Neve/Cab/Presence) plus a dedicated **Input trim** knob and a live **input
  level meter** in the VST LCD (green/yellow/red vs the -12..-6 dBFS DI target).
  The ZDL fixes Input at 1.0 - the pedal's hardware INPUT VOL does the level
  matching before the DSP, exactly like plugging into a tube amp.
- **Input trim**: 0..1 maps to 0.125x..1.25x input gain, with 1.0 = the
  calibrated reference (the historical fixed 1.25x). Set it once per take so
  every source hits the amp the same way.
- **Touch dynamics**: the gain stage's clip drive rides the input envelope,
  so soft picks stay clean and hard picks break up - a real amp responds
  to the hand, not a fixed transfer curve.
- **Power sag**: envelope-driven gain dip on transients (1 ms attack /
  200 ms release) gives the low-end "give" of a tube power amp.
- **Cabinet voicing**: speaker resonance (105 Hz + 3.5 kHz) plus a fixed
  filter bank (90 Hz HP, 180 Hz body, 3.2 kHz presence, 7-9 kHz rolloff) -
  no runtime coefficient math.
- **Neve coloration (knob)**: asymmetric saturation (even harmonics) +
  1073-style EQ (110 Hz shelf, 700 Hz mid, 12 kHz shelf) + DC block, blended
  wet/dry by the Neve knob (0 = bypass).

## Build

The Visual Studio generator's Windows-SDK probe hangs on this machine, so use
Ninja inside a vcvars environment:

```sh
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
cmake -S . -B build -G Ninja -DCMAKE_MAKE_PROGRAM="<VS>/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe" ^
      -DAMPNEVE_BUILD_VST=ON -DJUCE_ROOT="<path-to-juce-7.0.12>"
cmake --build build
ctest --test-dir build
```

- `test_ampsim` - numeric behavior + stability
- `tools/check_zdl_safe.py` - static ZDL-safety audit of the core
- `tools/di_level_check.py` - level-check a recorded DI take (peak/rms/clipping):
  python tools/di_level_check.py take.wav  (target peak -12..-6 dBFS, rms -20..-16 dBFS)
- `tools/ampsim_render` - offline renderer (same code path as VST/ZDL):
  `ampsim_render in.wav out.wav [input] [gain] [bass] [mid] [treble] [master] [level] [neve] [cab] [presence]`
- VST3: `build/vst/AmpNeveVST_artefacts/Release/VST3/AmpNeve.vst3`

## ZDL

See [zdl/README.md](zdl/README.md). Code is ready; the `.obj` build needs TI
C6000 CGT (`cl6x`) and hardware testing on a real pedal is pending.

## Relation to Reverson

Reverson (the reverse-reverb project) lives in the `zoomreverse` repo. This
repo is its sibling: run AmpNeve in front of Reverson for a full
"DI guitar -> amp/cab -> reverse reverb" chain.

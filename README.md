# AmpNeve - amp + cabinet + Neve-style coloration

A from-scratch, pedal-safe guitar amp simulator, built as an **independent
effect** (separate from the Reverson reverse reverb). It turns a DI /
preamp guitar take into a "through a clean amp + 1x12 cab + Neve console"
sound, and ships as a **VST3** plus a **Zoom G1on / MS-series ZDL**.

Original DSP - no extracted factory algorithms (boutique amp v1):

```
in -> input stage            light asymmetric saturation, DC block
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
- the DSP stays tiny (388 bytes of state) and trivially ZDL-safe.

## Features

- **ZDL-safe core** (`core/ampsim.c`): no heap, no `double`, no
  `sinf/cosf/powf/logf`, no division, no large writable statics. All filter
  coefficients are precomputed constants (`core/ampsim_coeffs.h`, generated
  by `tools/gen_coeffs.py`), so the audio path is pure multiply-add.
- **6 knobs, two pages x 3** (pedal style): P1 Gain/Bass/Mid,
  P2 Treble/Master/Level.
- **Touch dynamics**: the gain stage's clip drive rides the input envelope,
  so soft picks stay clean and hard picks break up - a real amp responds
  to the hand, not a fixed transfer curve.
- **Power sag**: envelope-driven gain dip on transients (1 ms attack /
  200 ms release) gives the low-end "give" of a tube power amp.
- **Cabinet voicing**: speaker resonance (105 Hz + 3.5 kHz) plus a fixed
  filter bank (90 Hz HP, 180 Hz body, 3.2 kHz presence, 7-9 kHz rolloff) -
  no runtime coefficient math.
- **Neve coloration (fixed)**: asymmetric saturation (even harmonics) +
  1073-style EQ (110 Hz shelf, 700 Hz mid, 12 kHz shelf) + DC block.

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
- `tools/ampsim_render` - offline renderer (same code path as VST/ZDL):
  `ampsim_render in.wav out.wav [gain] [bass] [mid] [treble] [master] [level]`
- VST3: `build/vst/AmpNeveVST_artefacts/Release/VST3/AmpNeve.vst3`

## ZDL

See [zdl/README.md](zdl/README.md). Code is ready; the `.obj` build needs TI
C6000 CGT (`cl6x`) and hardware testing on a real pedal is pending.

## Relation to Reverson

Reverson (the reverse-reverb project) lives in the `zoomreverse` repo. This
repo is its sibling: run AmpNeve in front of Reverson for a full
"DI guitar -> amp/cab -> reverse reverb" chain.

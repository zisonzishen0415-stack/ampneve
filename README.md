# AmpNeve - amp + cabinet + Neve-style coloration

A from-scratch, pedal-safe guitar amp simulator, built as an **independent
effect** (separate from the Reverson reverse reverb). It turns a DI /
preamp guitar take into a "through a clean amp + 1x12 cab + Neve console"
sound, and ships as a **VST3** plus a **Zoom G1on / MS-series ZDL**.

Original DSP - no extracted factory algorithms (boutique amp v15, strict
real-amp structure):

```
in -> input trim             level matching for any DI / loop take [Input]
   -> V1 input stage         fixed ~4x gain + soft asymmetric clip (the
                              first tube CAN clip on a hot DI), then a
                              Miller-cap lowpass @ 9 kHz
   -> V2 cold clipper        [Gain] knob drive + touch-dynamics envelope
                              (soft picks stay clean, hard picks break up),
                              JCM800-style asymmetric clip, Miller LP @ 5k
   -> Klon-style mix         the V1 clean tap (a clean amp tone that goes
                              through the tone stack + power amp below) is
                              blended with the V2 driven path - more clean
                              at low gain, distortion dominates at max
   -> tone network           interacting bass/mid/treble peaking
                              (fixed centers, linear-gain coeffs) [Bass/Mid/Treble]
   -> power amp              phase inverter (asymmetric clip) -> push-pull
                              soft clip (odd harmonics, NFB-shaped knee) +
                              sag (envelope-driven gain dip)    [Master]
   -> transformer            Neve-style even harmonics + 1073-style EQ
                              + DC block (fixed brand color)
   -> speaker                cabinet resonance (105 Hz + 3.5 kHz) + cab
                              dark..bright voicing (fixed blend)  [Cab]
   -> cabinet IR             1024-tap miked-cab convolution (SM57-style
                              mic + speaker-cone resonances + room) - the
                              link that makes it read as a real cab
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
- the DSP stays tiny (~4.7 KB of state, most of it the IR delay
  buffer) and trivially ZDL-safe.

## Features

- **ZDL-safe core** (`core/ampsim.c`): no heap, no `double`, no
  `sinf/cosf/powf/logf`, no division, no large writable statics. All filter
  coefficients are precomputed constants (`core/ampsim_coeffs.h`, generated
  by `tools/gen_coeffs.py`), so the audio path is pure multiply-add.
- **Nashville session voice**: tight low end (105 Hz resonance +2.5 dB),
  forward ~850 Hz mid, glassy top (full 4th-order 9.5/12 kHz lowpass) -
  the clean/edge platform studio country and modern shoegaze players use.
- **9 parameters** (VST and ZDL identical, three pages x three): P1
  Bass/Mid/Treble, P2 Gain/Master/Level, P3 Neve/Cabtype/Input, plus the
  BYPASS button and a live **input level meter** in the VST LCD
  (green/yellow/red vs the -12..-6 dBFS DI target). Presence is fixed at
  0.85 and is a no-op with the real IRs (the capture owns the top end).
- **Two real cabinets (Cabtype knob)**: 2x12 open-back (G12H30 + Blue)
  and 4x12 (Greenback family) - REAL sampled IRs from the Tubes&Tone pack
  (redistributable), 2048 taps / 46.4 ms keeping the near-field character
  and most of the room decay. The IRs carry the full miked-cab EQ, so the
  front chain is reduced to safety filters (HP 40 Hz / LP 16 kHz) and the
  speaker-resonance biquads are identity (the old Presence knob is now a
  no-op). Switching cabs is click-free: dual-buffer convolution crossfades
  over ~12 ms.
- **Presets (VST)**: a preset row above the pedal buttons - five factory
  presets (Nashville Clean / Edge-Breakup / British Crunch / High Gain /
  Emo-Edge) plus user presets saved as XML files in the OS app-data
  directory (`<appdata>/AmpNeve/Presets`). SAVE stores the current knob
  state (overwriting the selected user preset or creating a new one), DEL
  removes it, and the DAW's own plugin-state save (VST3 state) also works -
  sessions restore the last settings either way.
- **Reasonable defaults**: the factory patch is a balanced edge-of-breakup
  tone (gain 0.35, master 0.55, level 0.75, neve 1.0, cabtype 1x12) - pick
  softly for clean, dig in for crunch - so the first note through the
  plugin already shows the touch-dynamics character.
- **Input trim**: 0..1 maps to 0.125x..1.25x input gain, with 1.0 = the
  calibrated reference (the historical fixed 1.25x). Set it once per take so
  every source hits the amp the same way.
- **Touch dynamics**: the gain stage's clip drive rides the input envelope,
  so soft picks stay clean and hard picks break up - a real amp responds
  to the hand, not a fixed transfer curve.
- **Distortion that actually reaches the output, built like a real head**:
  gain is never crammed into one stage. V1 is a fixed ~4x stage that clips
  on hot input; the Gain knob drives a JCM800-style cold clipper (V2) with
  an asymmetric knee; the phase inverter clips before the push-pull power
  stage (odd-symmetric, NFB-shaped, so even harmonics cancel like a real
  output transformer); the Neve transformer adds its own even harmonics.
  Each tube stage is followed by a Miller-cap interstage lowpass (9 kHz
  after V1, 5 kHz after V2) - the same reason a cranked Soldano/Marshall
  stays warm instead of piercing: harmonics generated in one stage are
  shaped before the next stage multiplies them, and the 3-6 kHz harmonic
  cluster is tamed BEFORE the tone stack's treble/presence can boost it.
  Measured at max gain (440 Hz, post-cab, Nashville): h3 ~ -16 dB (stronger
  saturation than the previous single-stage design), while the harmonics
  that land in the 4-6 kHz presence band (h9-h13) are now equal to or
  quieter than v12 instead of 2-3 dB hotter - the "modern Friedman"
  upper-mid push is gone, the distortion body stays.
- **Klon-style clean/dirty blend, with the clean being a real amp tone**:
  the parallel "dry" is not the raw DI - it is the V1 clean signal that
  also passes the tone stack and the power amp, exactly like the clean
  floor of the same amp. The V1 tap and the V2 driven path are summed
  before the tone network, so both branches go through the same power amp
  and cab, sample-aligned by construction (no delay line). 40% clean at
  low gain tapering to 23% at max: the distortion dominates, but the pick
  attack and touch dynamics survive on top of it.
- **Power sag**: envelope-driven gain dip on transients (1 ms attack /
  200 ms release) gives the low-end "give" of a tube power amp.
- **Cabinet IR convolution**: speaker resonance (105 Hz + 3.5 kHz), a cab
  voicing chain (95 Hz HP, 220 Hz body, 550 Hz de-honk, 4.2-5.5 kHz cone
  presence, steep 4th-order LP) then a static 1024-tap miked-cab impulse
  response (SM57-style mic + damped speaker-cone resonances + room scatter,
  per voice). Convolution carries the time structure a real cab has - notes
  bloom and decay through the cone and mic - which no biquad chain can do.
  The IR is a precomputed static const kernel (`core/ampsim_coeffs.h`,
  generated by `tools/gen_coeffs.py`); the audio path is pure multiply-add.
- **Neve coloration (knob)**: asymmetric saturation (even harmonics) +
  1073-style EQ (110 Hz shelf, 700 Hz mid, 12 kHz shelf) + DC block, blended
  wet/dry by the Neve knob (0 = bypass).

## What makes AmpNeve different from a typical amp sim

Most amp sims are one big nonlinear transfer curve plus an IR loader. This
one is designed backwards: it has to fit in a Zoom G1on pedal (a C6000 DSP
with no OS, no heap, and no math library) **and** run as a desktop VST3, so
every design decision is shaped by that split:

- **One core, two radically different targets.** `core/ampsim.c` is the
  exact same code in the VST3 and the ZDL. That forces the audio path to be
  pure multiply-add: no heap, no `double`, no `sinf/cosf/powf/logf`, no
  division. All filter coefficients are precomputed constants
  (`core/ampsim_coeffs.h`, generated by `tools/gen_coeffs.py`); even the
  tone-knob coefficients update with a fixed-point reciprocal approximation
  and multiply-adds only.
- **The cabinet IR is synthesized, not sampled.** Ordinary sims ship a real
  miked-cab IR (or let you load one). Ours is built from parts: the old mic
  pickup's exact HP/LP response as a causal base, speaker-cone modal
  resonances (damped ringing), and a little room scatter - then baked into
  a static 1024-tap kernel per voice. It reads as a miked cab (notes bloom
  and decay through the cone) without shipping a copyrighted IR, and it is
  ZDL-safe by construction.
- **Touch dynamics instead of a fixed drive curve.** The gain stage's clip
  drive rides the input envelope, so soft picks stay clean and hard picks
  break up - the amp responds to the hand. Combined with the power-sag
  envelope (1 ms attack / 200 ms release) this gives the low-end "give" of
  a tube power amp.
- **Strict real-amp structure, not one big transfer curve.** Real
  high-gain preamps are several moderate-gain stages with interstage
  bandwidth limiting (Miller capacitance + RC filters between stages -
  the Soldano SLO design keeps the amp from sounding harsh by shorting
  highs and lows to ground between gain stages). Ours mirrors that: V1
  (fixed 4x) -> Miller LP @ 9k -> V2 cold clipper (Gain knob) -> Miller LP
  @ 5k -> tone stack -> phase inverter -> push-pull power stage. Each
  stage clips only a little; the harmonics accumulate and are shaped at
  every boundary, which is how a cranked amp gets rich-but-warm saturation
  instead of a flat-top square into the speaker.
- **Klon-style clean/dirty blend.** The parallel clean is the V1 signal
  through the same tone stack and power amp (a clean amp tone, not the
  raw DI), mixed before the tone network so both branches share the power
  amp and cab. 40% clean at low gain tapering to 23% at max - the pick
  attack survives at high gain, the same parallel clean/dirty idea as the
  Klon Centaur applied inside the amp.
- **Neve coloration placed before the cab, on purpose.** The 1073-style EQ
  and even-harmonic saturation sit before the speaker rolloff, so the
  harmonics are tamed by the cab instead of being harsh on top of it
  (A/B'd against post-cab placement).
- **Per-voice cabinets.** The two voices (Nashville / Emo-Edge) swap the
  Neve EQ, the cab voicing AND the cabinet IR itself - not just an EQ
  tweak. Each voice gets its own miked-cab character.
- **Calibrated input trim instead of a gain-forgiveness knob.** `Input`
  maps 0..1 to 0.125x..1.25x, with 1.0 = the calibrated reference for a
  -12..-6 dBFS DI, and the VST LCD shows a live input meter vs that target.
  On the pedal, Input is fixed at 1.0 and the hardware INPUT VOL does the
  level matching - exactly like plugging into a tube amp.
- **Pedal-shaped UI.** The VST's three-page knob layout and VOICE button
  mirror the Zoom pedal's LineSel pages, so a preset moves between the DAW
  and the pedal without re-learning the interface.
- **The whole DSP is ~4.7 KB of state** (most of it the IR delay buffer) -
  small enough that the ZDL arena doesn't blink, and the VST is a single
  tiny static binary with no runtime dependencies.
- **State memory that matches each target.** The VST saves/restores plugin
  state through the standard VST3 host mechanism AND keeps its own user
  presets as XML files (survives even hosts that do not restore state).
  The ZDL needs no preset system: the pedal stores every effect parameter
  inside the saved patch, so saving a patch on the pedal is exactly
  "remembering the last state" - power off/on and the knobs come back.

Known honest trade-off: at extreme gain+master+level the power stage
saturates fully and the output can approach 0 dBFS - set the Level knob
for the interface, like a real amp into a real desk.

## Build

Builds on this machine use the MSYS2 ucrt64 toolchain (gcc/g++ 13+, cmake,
ninja) with a JUCE checkout (7.0.12) for the VST3 shell. The VST is linked
statically so the plugin has no runtime DLL dependencies - a missing
`libstdc++`-family DLL was the original reason Ableton failed to load it.

```sh
export PATH="/c/Users/<user>/AppData/Local/Programs/MSYS2/ucrt64/bin:$PATH"
cmake -S . -B build -G Ninja \
      -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ \
      -DAMPNEVE_BUILD_VST=ON -DJUCE_ROOT="<path-to-juce-7.0.12>" \
      -DCMAKE_SHARED_LINKER_FLAGS="-static -static-libgcc -static-libstdc++" \
      -DCMAKE_MODULE_LINKER_FLAGS="-static -static-libgcc -static-libstdc++" \
      -DCMAKE_EXE_LINKER_FLAGS="-static -static-libgcc -static-libstdc++"
cmake --build build
ctest --test-dir build
```

Install the VST3 to the system directory (as Administrator, with Ableton
closed so the DLL is not locked):

```sh
cmake --install build
# or copy the built bundle:
# build/vst/AmpNeveVST_artefacts/Release/VST3/AmpNeve.vst3
# -> "C:\Program Files\Common Files\VST3\AmpNeve.vst3"
```

### VST rebuild lessons (2026-08-14)

Two hard-won facts from a crash-hunting session:

1. **Never mix objects from different builds when hand-linking.** Hand-linking
   with JUCE objects from an older build plus freshly compiled ones produced a
   DLL that loaded but crashed REAPER at instantiation (AV in
   `juce::TextEditor::mouseDoubleClick`, heap-corruption signature). The
   crash disappeared after recompiling **every** object from source. When
   bypassing CMake/ninja (they can hang in sandboxed environments that block
   child-process IPC), always do a full clean rebuild: core + PluginProcessor
   + PluginEditor + all JUCE module objects + the 8 VST3 client objects +
   the .res, then relink. The scratch scripts used for this are
   `build/_full_rebuild.ps1` and `build/_relink.ps1` (in the ignored `build/`
   dir - extract what you need from `build_ninja/build.ninja`).
2. **REAPER caches VST3 module metadata by DLL hash.** Replacing a plugin DLL
   under a stale `reaper-vstplugins64.ini` entry can crash REAPER on
   instantiation. After installing a new build, clear the cache or re-scan
   (`Options -> Preferences -> Plug-ins -> VST`).

Verify a fresh build before installing: `build/test_ampsim_cur.exe` (36
tests), then check the DLL actually contains the v16 IR kernel bytes and the
`v16 IR` UI label (the label is drawn in the LCD header - if you see it, the
loaded plugin is current).

- `test_ampsim` - numeric behavior + stability
- `tools/check_zdl_safe.py` - static ZDL-safety audit of the core
- `tools/di_level_check.py` - level-check a recorded DI take (peak/rms/clipping):
  python tools/di_level_check.py take.wav  (target peak -12..-6 dBFS, rms -20..-16 dBFS)
- `tools/ampsim_render` - offline renderer (same code path as VST/ZDL):
  `ampsim_render in.wav out.wav [input] [gain] [bass] [mid] [treble] [master] [level] [neve] [cab] [presence] [voice]`
- VST3: `build/vst/AmpNeveVST_artefacts/Release/VST3/AmpNeve.vst3`

## ZDL

See [zdl/README.md](zdl/README.md). Code is ready; the `.obj` build needs TI
C6000 CGT (`cl6x`) and hardware testing on a real pedal is pending.

## Relation to Reverson

Reverson (the reverse-reverb project) lives in the `zoomreverse` repo. This
repo is its sibling: run AmpNeve in front of Reverson for a full
"DI guitar -> amp/cab -> reverse reverb" chain.

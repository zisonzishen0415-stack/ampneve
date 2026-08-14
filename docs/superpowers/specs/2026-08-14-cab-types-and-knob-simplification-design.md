# Cab Types + Knob Simplification (v17)

Date: 2026-08-14. Status: user-approved (iterated in conversation).

## Goal

- Add selectable cabinet types (1x12 / 2x12 / 4x12) on BOTH the VST and the
  Zoom ZDL, replacing the single synthesized 1x12.
- Simplify the control surface to exactly 9 params (the ZDL firmware
  ceiling) and drop the Voice (Nashville/Emo) distinction.

## Final parameter layout (VST and ZDL identical)

| Page | Knobs |
|---|---|
| P1 | Bass / Mid / Treble |
| P2 | Gain / Master / Level |
| P3 | Neve / CABTYPE (1x12/2x12/4x12) / Input |

Fixed internally (no knob): Presence 0.85, cab dark/bright blend 0.5,
Input trim calibration unchanged, voice = Nashville character only.

## Cab synthesis (tools/gen_coeffs.py)

Refactor `synth_cab_ir` into per-speaker + room stages:

- `synth_speaker(mic_sections, resonances, seed, phase, delay)` -> one
  speaker's dry IR (mic base + cone modal ringing, per-speaker random
  phase + mic distance delay).
- Cab = sum of N speakers, then ONE room stage on the sum (freq-dependent
  scatter + specular early reflections + all-pass dispersion + window +
  loudness match), matching how a mic hears a cab through one room.
- Cab parameters:
  - 1x12: N=1, current Nashville resonance table (350/1150/1800/2800/5600).
  - 2x12: N=2, cone modes slightly lower (320/1050/1700/2600/5400),
    speaker 2 at +0.4 ms mic distance (subtle comb in 1-2 kHz).
  - 4x12: N=4, lower modes (300/950/1600/2400/5000), delays
    0/0.35/0.7/1.0 ms, slightly more room.
- Per-cab voicing chains (5 biquads: HP95, body220, midcut550, presence,
  4th-order LP) and the 105 Hz + 3.5 kHz resonance biquads, tuned per cab
  (2x12/4x12: more low-mid body, smoother top).
- Emo voice removed: single Nashville-voiced IR set per cab
  (3 x 1024 floats const, down from 4 KB x 2 voices).

## Core (core/ampsim.c / ampsim.h)

- `AMP_PARAM_VOICE` removed; `AMP_PARAM_CABTYPE` added (0..2).
- Cabtype selects: resonance biquads, single voicing chain, and IR pointer
  (one set per cab type; the dark/bright blend loop is replaced by the
  selected cab's single chain).
- Gain base fixed at the Nashville value (0.2); the voice-switch state and
  `update_voice_coeffs` are removed.
- Presence stays an API param (set to 0.85 by the UIs), Neve stays a knob.

## ZDL

- manifest: exactly 9 params (bass, mid, treble, gain, master, level,
  neve, cabtype, input). Input is now a knob (was fixed 1.0).
- wrapper: cabtype + input read from params; voice fixed Nashville; no
  presence knob (presence set to 0.85 in init).
- build: unchanged toolchain; audit must stay SAFE.

## VST

- APVTS: 9 params (bass, mid, treble, gain, master, level, neve, cabtype
  0..2, input); presence/cab internals removed from the UI.
- Editor: P3 shows Neve / Cabtype / Input knobs; VOICE button removed;
  LCD shows the current cab type. Factory presets updated.

## Verification

- test_ampsim: Emo tests removed; new cabtype tests (IR selection per
  cabtype, no NaN, cabtype switch mid-stream does not pop); all pass.
- ZDL: obj audit SAFE, 9 params accepted by the linker.
- VST: full clean rebuild, loads in REAPER, `v17` label + cab shown.
- A/B renders: 3 cabs x default preset.

## Out of scope

- User-loadable IRs, oversampling, speaker nonlinearity.

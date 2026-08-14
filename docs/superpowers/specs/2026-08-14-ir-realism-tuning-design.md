# Cabinet IR Realism Tuning (v16)

Date: 2026-08-14. Status: approved by user (2026-08-14, "我认为是的，并且需要注重真实感").

## Goal

Retune the synthesized cabinet IR kernels (`tools/gen_coeffs.py` ->
`core/ampsim_coeffs.h`) so the IR complements the v15 amp head instead of
fighting it, with realism (time structure, room, phase) as the guiding
principle. No changes to `core/ampsim.c`; the IR stays 1024 taps and
static-const (ZDL-safe).

## Evidence that drove the design

Cascaded measurement (reso_low -> cab_dark chain -> IR, Nashville):

- 150 Hz IR mode stacks with the 105 Hz speaker resonance and the 220 Hz
  cab body: the 150-300 Hz band sits at +2.4..+4.0 dB (boxy), and the
  1024-tap IR only resolves ~43 Hz/bin there - the low end belongs to the
  IIR chain.
- 900 Hz cone bloom sits 50 Hz above the tone stack's 850 Hz Mid center:
  a 3.5 dB step at flat, double-stacked when Mid is boosted.
- The IR's high end is dark (about -1.3 dB at 5.8 kHz), so its "SM57
  paper" character is inaudible under the IIR glass (4 kHz).
- The IR is nearly minimum-phase; the head's phase through LP2@5 kHz is
  clean, which reads "digital" - real cones smear phase.

## Changes (all in tools/gen_coeffs.py)

1. Low end yields to the IIR chain:
   - Nashville: remove the 150 Hz mode (150, tau 6 ms, +3.0 dB).
   - Emo/Edge: 160 Hz 3.0 -> 1.5 dB (keep a hint of body).
2. Mid detuned off the 850 Hz tone center:
   - both voices: 900 Hz -> 1150 Hz; Nashville +2.5 -> +2.0, Emo +3.0 -> +2.0.
3. Emo 420 Hz +2.5 -> +1.5 dB (keeps midwest-emo body, less stacking with
   the body220/body500 chain).
4. 2.8 kHz edge ring unchanged (+0.6 dB already meets the <= +0.8 rule).
5. High-end mic "paper":
   - Nashville: add (5600 Hz, tau 1.2 ms, +1.5 dB).
   - Emo: add (5200 Hz, tau 1.2 ms, +1.0 dB) - warmer identity preserved.
6. Room realism:
   - scatter level 0.015 -> 0.030;
   - frequency-dependent decay: split at 1 kHz, low tau 12 ms / high tau
     4 ms, energy 60/40 (highs die first, like a real room);
   - three discrete early reflections at 1.2 / 2.3 / 3.8 ms at
     -18 / -22 / -26 dB relative to the IR peak, shaped by the mic base
     response (shallow comb filtering, "amp in a room" instead of dry).
7. Phase dispersion: two 2nd-order all-pass filters (1.5 kHz Q 0.7,
   3.2 kHz Q 1.2) applied to the IR - zero magnitude change, pure time
   smear, speaker-cone-like phase rotation.

## Success criteria (all measurable)

- Combined response: 220 Hz peak <= +2.5 dB (was +4.0); the 850/900 Hz
  step gone; IR in-band still within +-1.5 dB.
- Phase deviation from minimum phase in 200 Hz-5 kHz increases vs the
  pre-change kernel (was ~93 deg, mostly the 0.68 ms delay).
- IR decay lengthens (room tau 12 ms low band), tail still windowed
  without a click; energy lost to the window < 1%.
- `test_ampsim` passes unchanged (structural tests).
- ZDL smoke + release builds stay SAFE in `tools/check_zdl_obj.py`
  (no new sections, .fardata still 0 bytes).
- v16 render set (same parameters as the v15 set) gives an A/B pair for
  every preset.

## Out of scope

- IR length change (1024 taps kept), oversampling, user-loadable IRs,
  speaker nonlinearity - all remain future work.

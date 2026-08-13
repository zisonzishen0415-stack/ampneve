# AmpNeve Boutique Amp Design

Goal: a boutique-quality independent amp that feels like a real amp, not an
EQ + clipper. Target voice: **Nashville session-player clean** (Brent Mason /
Vince Gill / modern studio country) with strong touch dynamics (pick softly =
clean, pick hard = edge-of-breakup). Works as the clean/edge platform for
DIIV / modern-shoegaze chains too.

## Signal chain (v1)

```
in
 -> input stage      light asymmetric saturation (even harmonics, level)
 -> gain stage       drive; clip threshold modulated by the envelope
                      (touch dynamics: soft picks stay clean)
 -> tone network     bass / mid / treble, interacting (3 biquads,
                      fixed center freqs, linear-gain coefficients)
 -> power stage      softer saturation + sag (envelope-driven gain dip
                      on transients), driven by Master
 -> transformer      Neve-style even harmonics + DC block (brand color)
 -> speaker          cabinet resonance (105 Hz + 3.5 kHz peaks) + cab
                      dark..bright voicing + mic mix
 -> level
```

All stages are ZDL-safe: polynomial saturation, one-pole envelopes,
precomputed-constant biquads. No heap, no double, no math library, no
division in the audio path.

## Nashville voicing (v2a)

The v1 voicing was boomy in the low-mid (+4.8 dB at 105 Hz), had a honky
presence hump at 2.7-3.8 kHz, and rolled the top end off too early (the cab
lowpass was only half-applied - see bug below). A clean arpeggio through it
sounded uncanny. Retuned for the Nashville studio voice:

| Region | v1 | v2a (Nashville) | Why |
|---|---|---|---|
| 105 Hz speaker reso | +4 dB Q1.5 | +2.5 dB Q1.2 | tight, no boom |
| 180 Hz cab body | +3 dB | +1.5 dB | no boxiness |
| 3.2 kHz presence | +2.5/+5 dB | +1.5/+3 dB | no honk |
| cab lowpass | 7/9 kHz (bug: 1 section) | 10/12 kHz, full 4th order | glass, no fizz |
| Neve LF / mid / HF | +2.5/+1.5/+1 dB | +1.5/+1/+1.5 dB | tight lows, extra air |
| Tone Bass / Mid / Treble | 250 / 700 / 3000 | 140 / 850 / 5000 | forward cut, no 3k honk |

Dynamics softened for clean: envelope release 30 -> 60 ms (no pump on
arpeggios), drive modulation 0.55+0.65*env -> 0.65+0.45*env, sag amount
0.45 -> 0.30, input-stage saturation 0.10/0.03 -> 0.07/0.02.

### Bug fixed: cab lowpass was half-applied

`gen_coeffs.py` writes a 4th-order lowpass as two cascaded 2nd-order
sections (5 biquads total with HP/body/presence), but the core looped over 4
and dropped the second LP section, so the top end stayed ~12 dB too bright
above 7-9 kHz. The loops now use `AMP_CAB_DARK_N` / `AMP_CAB_BRIGHT_N` (= 5)
and the struct arrays are sized from the same constants.

## Why these stages (boutique rationale)

| Stage | What it adds | Reference |
|---|---|---|
| Input stage | slight even-harmonic bloom before gain; makes the drive sit "on top" of a clean floor | Dumble ODS input |
| Gain stage + envelope | touch dynamics: soft = clean, hard = crunch | Two Rock / Dumble core behavior |
| Tone network | interacting Bass/Mid/Treble instead of fixed EQ | passive Fender/Marshall tone stack feel |
| Power stage + sag | compression and low-end "give" on loud hits | tube power amp |
| Transformer | low-frequency saturation and harmonic richness | output transformer |
| Speaker resonance | cabinet "bark" at 105 Hz and cone presence at ~3.5 kHz | real cab IRs |
| Neve coloration | final console sheen (brand) | 1073 |

## Parameters (9 knobs, three pages)

| Page | Knob | Range | Maps to |
|---|---|---|---|
| P1 | Bass | 0..1 | tone-network low, 140 Hz (0.5 flat) |
| P1 | Mid | 0..1 | tone-network mid presence, 850 Hz (0.5 flat) |
| P1 | Treble | 0..1 | tone-network high, 5 kHz (0.5 flat) |
| P2 | Gain | 0..1 | preamp gain + clip level (touch dynamics base) |
| P2 | Master | 0..1 | power-stage drive + sag amount |
| P2 | Level | 0..1 | output (0.5..1.5 gain) |
| P3 | Neve | 0..1 | Neve coloration wet/dry (0 = bypass) |
| P3 | Cab | 0..1 | cabinet voicing dark..bright |
| P3 | Presence | 0..1 | speaker 3.5 kHz resonance amount |

## ZDL-safe implementation notes

- Saturation curves: piecewise polynomials, different per stage
  (input: x - a x^3; gain: x - a x^3 + b x^2 with dc block; power: softer
  clip3-like). No tanh (no division / math lib).
- Envelope: `env += (|x| - env) * coeff` (one-pole, multiply-add).
- Tone network: peaking/shelf biquads with fixed center frequencies; the
  gain parameter is linear (0.5..2.0) so coefficients update with
  multiply-add only (no pow, no sin/cos at runtime - w0 is constant).
- Speaker resonance: fixed-coefficient biquads.
- State: ~1 KB total (fits ctx[3] easily).

## Roadmap

- v1: multi-stage gain + touch dynamics + tone network + power sag +
  speaker resonance; 6-knob Gain/Bass/Mid/Treble/Master/Level.
- v2a: Nashville session voicing (see above) + cab lowpass bug fix.
- v2b: 9 knobs / three pages - Neve, Cab and Presence exposed as knobs.
- v2 (future): Voice 1..5 voicing presets (Clean American / British
  crunch / Dumble-ish / High-gain / Boutique clean), dual-mic cabinet
  (dynamic close + condenser room), presence/depth.

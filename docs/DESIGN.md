# AmpNeve Boutique Amp Design

Goal: a boutique-quality independent amp that feels like a real amp, not an
EQ + clipper. Target voice: DIIV / modern-shoegaze clean-to-light-crunch
with strong touch dynamics (pick softly = clean, pick hard = break up).

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

## Parameters (6 knobs, two pages)

| Knob | Range | Maps to |
|---|---|---|
| P1 Gain | 0..1 | preamp gain + clip level (touch dynamics base) |
| P1 Bass | 0..1 | tone-network low shelf (-..+ around center) |
| P1 Mid | 0..1 | tone-network mid peaking |
| P2 Treble | 0..1 | tone-network high shelf |
| P2 Master | 0..1 | power-stage drive + sag amount |
| P2 Level | 0..1 | output (0.5..1.5 gain) |

Neve coloration is fixed internally (brand sound) in v1; a Voice/Neve knob
may return in v2 with 1..5 voicing presets.

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

- v1 (this pass): multi-stage gain + touch dynamics + tone network +
  power sag + speaker resonance; 6-knob Gain/Bass/Mid/Treble/Master/Level.
- v2 (future): Voice 1..5 voicing presets (Clean American / British
  crunch / Dumble-ish / High-gain / Boutique clean), dual-mic cabinet
  (dynamic close + condenser room), presence/depth.

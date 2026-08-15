# AmpNeve Boutique Amp Design

Goal: a boutique-quality independent amp that feels like a real amp, not an
EQ + clipper. Target voice: **Nashville session-player clean** (Brent Mason /
Vince Gill / modern studio country) with strong touch dynamics (pick softly =
clean, pick hard = edge-of-breakup). Works as the clean/edge platform for
DIIV / modern-shoegaze chains too.

## Signal chain (v15)

```
in
 -> V1 input stage   fixed ~4x gain, soft asymmetric clip (grid/plate
                      clipping on hot DI), Miller LP @ 9 kHz
 -> V2 cold clipper  Gain-knob drive modulated by the touch envelope,
                      JCM800-style asymmetric clip, Miller LP @ 5 kHz
 -> Klon mix         V1 clean tap + V2 driven path, summed before the
                      tone network (both pass the power amp and cab)
 -> tone network     bass / mid / treble, interacting (3 biquads,
                      fixed center freqs, linear-gain coefficients)
 -> power amp        phase inverter (asymmetric clip) -> push-pull
                      soft clip (odd harmonics, NFB-shaped) + sag
 -> transformer      Neve-style even harmonics + DC block (brand color)
 -> speaker          cabinet resonance (105 Hz + 3.5 kHz peaks) + cab
                      dark..bright voicing + miked-cab IR convolution
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

## Gain staging (v7)

The drive/sag coefficients were raised to make the amp saturate like a
working tube power amp instead of a preamp-then-clean output:

| Stage | v6 | v7 | Effect |
|---|---|---|---|
| Gain drive | base + 2.6*gain | base + 3.0*gain | slightly hotter preamp ceiling |
| Power drive | 0.5 + 1.3*master | 0.5 + 2.2*master | power stage saturates harder |
| Sag amount | 0.30*master | 0.40*master | more low-end "give" on transients |

Consequence (measured, 220 Hz sine, -12 dBFS input): the cranked
gain=0.8/master=0.8 loud/soft RMS compression ratio drops from ~1.9 to
~1.1 - heavy power-amp squash at full drive - while edge-of-breakup
(gain/master ~0.55) stays at ~1.3, preserving pick dynamics.

## Gain staging (v8) - actually reaching distortion / fuzz

Two problems showed up on a real DAW (Ableton): the max-gain tone never
reached distortion/fuzz, and turning the tone knobs to extremes produced a
severe block-rate buzz.

| Problem | Root cause | Fix |
|---|---|---|
| Extreme-tone "zizi" buzz | the VST re-sends every param each audio block; `tone_peaking` zeroed the biquad states on every call, so the tone filters clicked at block boundaries (worst at extreme settings) | Bass/Mid/Treble `set_param` now only recomputes when the value actually changes (same guard the voice switch already had) |
| Max gain won't distort | the gain-stage `clip3` is a soft cubic (weak harmonics: h3 ~ -21 dB at the output) | all three saturating stages share one warm C1-continuous saturator (linear zone -> wide cubic knee -> gentle tail with a safety ceiling), monotonic and ZDL-safe |
| Max gain still not enough | drive ceiling 2.8x only; quiet DI's never reached the clip | drive mapping is now `base + 3.0*gain + 8.0*gain^2` - clean at low knob, 11x+ at max |
| Distortion eaten by the cab | the synthesized cabinet IR's room scatter created deep narrow nulls (~-17 dB at 600 Hz) that removed the drive harmonics | regenerated the IR with gentler room scatter; mid-band response is now flat within ~4 dB, harmonics survive |
| Emo/Edge voice never distorts | its IR was ~12 dB quieter than Nashville (comb-nulled lows) | both IRs are loudness-matched over the 80..5000 Hz guitar band |

Measured at max gain (220 Hz, -12 dBFS): the output 3rd harmonic went from
~-20.5 dB to -13.8 dB (Nashville) / -10.2 dB (Emo/Edge) - real distortion,
not a rounded soft clip - and the 7th/9th harmonics dropped to ~-34/-46 dB
(a piecewise-linear clipper trial sounded "digital": h7/h9 at -20 dB). The
power-stage drive was reduced to 0.5 + 1.0*master and the transformer to
1.4x so those stages shape in the knee instead of re-flattening the wave
into a square (flat-top clipping is what produces the buzz).

## Strict real-amp modeling (v11)

The v9/v10 trials bounced between "too smooth to hear clipping" and
"digital buzz". The fix is to model each stage like a real amp instead of
one universal clipper:

| Stage | Real-amp role | Model |
|---|---|---|
| Input stage | V1 first tube: gain, clips on hot input | gain 1.4x through the light asymmetric polynomial, then a soft clip (effective knee ~0.83/1.33) |
| Gain stage | preamp drive | touch-dynamics drive into an asymmetric Tube-Screamer-style soft clip: positive clips at 0.5, negative at 0.8 (even harmonics = audible grit), C1 cubic knees |
| Power stage | power tubes: drive + sag + soft clipping | drive 0.5 + 1.2*master into the same soft clip family, sag envelope on top |
| Transformer | output transformer saturation | 1.2x into the soft clip plus the even-harmonic c1 + 0.15*c2^2 mix |

The harmonic signature at max gain is now the classic soft-clip profile
(~h3 -12 dB, h5 -16 dB, h7 -24 dB for Nashville; h3 ~-9 dB for Emo/Edge) -
audible clipping texture, not compression masquerading as drive. The dry
mix is gain-dependent (40% dry at low gain -> 15% at max) so the distortion
is not diluted, and the Level knob maps to 0.20..0.90x so the default patch
peaks near -1 dBFS with a -12 dBFS DI.

## Drive range (v12) - quiet inputs must break up too

A real preamp has tens of dB of gain; ours capped at ~13x (+22 dB), so a
quiet DI (below ~-28 dBFS) never reached the clip knee and "gain maxed"
still sounded clean. The drive mapping is now `base + 4*gain + 56*gain^2`
(~60x / +35.5 dB at max): gain=1 distorts down to about -30 dBFS input
(measured h3 = -18.8 dB at -30 dBFS), while the quadratic taper keeps the
default clean. The default gain is 0.25 (edge-of-breakup) so the factory
patch has room in both directions.

## Strict real-amp structure (v15) - multi-stage gain + interstage limiting

The v12 drive (60x in ONE clip) measured well but sounded harsh: a single
flat-top clip feeds all its harmonics straight into the speaker, and that
is exactly what real high-gain amp designers spend their circuit budget
avoiding. The research that drives v15:

- **Multistage preamp with interstage bandwidth limiting.** A real
  high-gain preamp (Soldano SLO, JCM800) is several moderate-gain stages;
  between the stages, RC networks short the highs and lows to ground "to
  keep the amp from sounding harsh" (SLO service notes). Each 12AX7 stage
  also has its own Miller capacitance pole - a typical stage with a 68k
  source clips around 15.5 kHz (AikenAmps, "What is Miller Capacitance").
  Net effect: harmonics made in one stage are rolled off before the next
  stage multiplies them, so the accumulated saturation is rich in the
  guitar band but dies above ~6-9 kHz.
- **Cold-biased second stage.** The JCM800's classic crunch comes from a
  stage biased near cutoff (10k cold cathode): the positive half conducts
  and clips early, the negative half is attenuated - a strongly asymmetric
  clip that produces the "spitty" Marshall texture. Our V2 is exactly that:
  asymmetric clip_ts (positive knee 0.5, negative 0.8) driven by the Gain
  knob.
- **Phase inverter + push-pull power amp with negative feedback.** The
  Marshall power amp's NFB loop surrounds the PI, the power tubes and the
  output transformer. Push-pull cancels even harmonics (odd-symmetric
  transfer), NFB extends headroom and softens the knee. Our power stage:
  clip_ts (PI, asymmetric) then clip_pp - an odd-symmetric cubic soft clip
  `x - x^3/3` with a saturating tail (designed and verified in
  `work/design_pp.py`): h3 is the dominant harmonic, evens cancel, and
  there is no flat-top square.

v15 structure (all measured, 220 Hz sine, gain=1 master=1, post-cab):

| Stage | Model | Miller/limiting LP |
|---|---|---|
| V1 input | fixed ~4x, soft asymmetric clip | 9 kHz (2nd order) |
| V2 cold clipper | `base + 1.5*gain + 16*gain^2` drive (max ~17x), touch-dynamics envelope, asymmetric clip | 5.0 kHz (2nd order) |
| Klon mix | V1 clean tap 40%->23% vs driven 60%->77%, summed pre-tone-stack | - |
| Tone stack | 140 / 850 / 5 kHz peaking (FMV position) | - |
| Power amp | PI clip_ts -> push-pull clip_pp (odd, NFB-shaped) + sag | - |
| Transformer | even-harmonic mix + 1073 EQ | - |

Harmonics at max gain, post-cab (220 Hz sine, v15 vs v12):

| harmonic | v15 | v12 | note |
|---|---|---|---|
| h3 (660 Hz) | -20.8 dB | -21.7 dB | distortion depth kept (stronger) |
| h5 (1.1 kHz) | -22.3 dB | -23.1 dB | same |
| h7 (1.5 kHz) | -26.9 dB | -27.6 dB | same |
| h20 (4.4 kHz) | -51.7 dB | -47.1 dB | fizz starts dropping |
| h30 (6.6 kHz) | -57.9 dB | -52.3 dB | -5.6 dB less piercing top |
| h40 (8.8 kHz) | -66.2 dB | -60.4 dB | -5.8 dB |

And the "modern/Friedman" cluster test (440 Hz sine, harmonics that land
in the 3-6 kHz presence band):

| harmonic | v15 | v12 | what it means |
|---|---|---|---|
| h3 (1.3 kHz) | -16.4 dB | -20.0 dB | strong saturation body |
| h5 (2.2 kHz) | -22.3 dB | -25.4 dB | body kept |
| h7 (3.1 kHz) | -22.0 dB | -24.6 dB | upper-mid grit, kept strong but no boost |
| h9 (4.0 kHz) | -23.5 dB | -25.0 dB | presence band tamed (+1.5 dB, was +2.6) |
| h11 (4.8 kHz) | -27.8 dB | -28.3 dB | now ~equal to v12 (was +2.5 dB) |
| h13 (5.7 kHz) | -27.4 dB | -27.5 dB | equal (was +2.2 dB) |

Early v15 left the V2 clipper's 3-6 kHz harmonic cluster (~h7..h13 of a
440 Hz note) 2-3 dB hotter than v12, and the tone stack's 5 kHz treble +
the cab/IR presence bumps (4-6 kHz) stacked on top - that combination is
the "modern/Friedman" upper-mid push. v15b tames it where real amps do:

- LP2 lowered 6.5 kHz -> 5.0 kHz so the cluster is shaped BEFORE the tone
  stack boosts 5 kHz (real high-gain interstage networks roll off well
  below 6.5 kHz);
- the synthesized IR's own 4-6 kHz bump flattened (mic EQ 5.8 kHz +3 ->
  +1 dB, 2.8 kHz cone ring reduced);
- cab presence peaks reduced ~1 dB.

Clean frequency response (weak input, normalized to 1 kHz): the 4 kHz
presence is +2.5 dB (was +5.5), 5-6 kHz is ~flat (was +2..+6), above 8 kHz
it falls steeply - a warm vintage-style top end instead of the modern
presence hump, while the 3-4 kHz band (where Nashville "glass" lives) is
still lifted. The Klon clean tap is the V1 signal through the same tone
stack/power amp/cab (not the raw DI), so at max gain the output stays
warm-saturated while the pick attack survives (cranked loud/soft
compression ratio ~1.03, edge-of-breakup ~1.7).

## Klon-style clean/dirty blend (v15)

The drive path is not strictly serial: the V1 clean tap (the signal that
only went through the input stage, BEFORE the V2 clipper) is blended with
the V2 driven path and the sum goes through the tone stack, power amp and
cab together. So the "dry" is a real clean amp tone through the same
power stage - not the raw guitar DI - which is what makes the mix sound
like the same amp clean vs dirty rather than a DI overdub. The mix happens
before the tone network, so both branches share the tone stack, the phase
inverter, the push-pull stage, the transformer and the cab, and are
sample-aligned by construction (no delay line needed - 32 samples of state
removed vs v8). 40% clean at gain 0 tapering to 23% at max: the pick
attack and touch dynamics survive at high gain (cranked loud/soft ratio
~1.03-1.04, edge-of-breakup ~1.7), while the distortion dominates.
Level maps to 0.20..0.90x so the default patch leaves headroom (a
-12 dBFS DI peaks near -1 dBFS instead of clipping the bus).

### Bug fixed: cab lowpass was half-applied

`gen_coeffs.py` writes a 4th-order lowpass as two cascaded 2nd-order
sections (5 biquads total with HP/body/presence), but the core looped over 4
and dropped the second LP section, so the top end stayed ~12 dB too bright
above 7-9 kHz. The loops now use `AMP_CAB_DARK_N` / `AMP_CAB_BRIGHT_N` (= 5)
and the struct arrays are sized from the same constants.

## Cabinet IR realism tuning (v16)

Cascaded measurement (reso_low -> cab chain -> IR) showed the old IR
fighting the head: its 150 Hz mode stacked on the 105 Hz resonance + 220 Hz
body (+4 dB band), its 900 Hz cone bloom sat 50 Hz above the 850 Hz Mid
center, and its high end was dark where the IIR glass lives. The v16 IR
(see docs/superpowers/specs/2026-08-14-ir-realism-tuning-design.md) fixes
the division of labour:

- Low end (80-300 Hz) is the IIR chain's job: the 150 Hz IR mode is gone
  (Emo's 160 Hz halved), and the room scatter is high-passed at 180 Hz.
- 900 Hz -> 1150 Hz clears the Mid center; the 850 Hz IR contribution
  drops +1.5 -> +1.1 dB.
- SM57 "paper" added at 5.6 kHz (Nashville +1.5 / Emo 5.2 kHz +1.0,
  tau 1.2 ms) under the IIR glass.
- Room: scatter 0.028 with frequency-dependent decay (1 kHz split,
  low tau 16 ms / high tau 5 ms) + three SPECULAR early reflections
  (1.2/2.3/3.8 ms @ -22/-26/-30 dB, mic-colored copies of the dry
  impulse). Noise-burst reflections were tried and measured worse: flat
  spectra beat against the base per-bin and doubled the narrowband ripple.
- Two 2nd-order all-passes (1.5 kHz Q0.7 / 3.2 kHz Q1.2): cone time smear,
  0.0000 dB magnitude ripple verified.

Measured after: narrowband ripple (300-5 kHz) 1.31 -> 0.77 dB, decay -40 dB
at 15.9 ms, low end negative, 5.5 kHz +1.1 dB. A/B renders: out/v15_*.wav
(old) vs out/v16_*.wav (new), same parameters.

## Real cabinet IRs (v18)

User feedback: the synthesized cabs "都不怎么真实" - parameter-guessed cone
modes and modeled rooms cannot reach the detail of a real capture. Replaced
the synthesized kernels with two REAL sampled IRs from the Tubes&Tone pack
(user-confirmed redistributable):

- Cabtype 0 = 2x12 open-back (G12H30 + Blue), `ir/5T G12H30+BLU.44.1.wav`
- Cabtype 1 = 4x12 (Greenback family + 1x8), `ir/5T 412M25+108F59.44.1.wav`

Both 44.1 kHz mono captures, flat within +-2 dB from 80 Hz to 7 kHz
(smoothed), no deep nulls. Truncated to 2048 taps (46.4 ms, near-field +
most of the room decay) with an 8 ms squared end window and loudness-
matched to unity mid-band (300..3000 Hz) gain so the amp's level staging
is unchanged.

The real IRs own the full miked-cab character, so the front chain
degenerates to SAFETY filters only (HP 40 Hz rumble + LP 16 kHz hiss +
identity) and the speaker-resonance biquads are identity - the Presence
blend is therefore a no-op by design. The click-free cab crossfade
(dual 2048-tap delay buffers, ~12 ms fade) is unchanged. State: 16976 B
per channel; ZDL reserves 4352 floats.

A/B renders: out/v18_2x12_default.wav vs out/v18_4x12_default.wav (same
amp settings, the two real cabs).

## Cab types + knob simplification (v17)

- Cabtype param (0..2): 1x12 / 2x12 / 4x12 synthesized miked-cab kernels.
  Multi-speaker synthesis: each cone gets its own ring phase and a
  mic-distance delay (2x12: +0.4 ms; 4x12: 0/0.35/0.7/1.0 ms -> subtle comb
  in 1-2 kHz); one room stage on the summed cab. Per-cab speaker resonance
  and voicing chain (2x12/4x12: lower cone modes, more low-mid body).
- Click-free cab switching: dual 1024-tap delay buffers (both always
  written) + a ~12 ms crossfade between the old and new kernels; biquad
  state is preserved on switch (bq_load_keep). State grew to 8784 B.
- Control surface cut to 9 params (the ZDL ceiling), identical on VST and
  ZDL: P1 Bass/Mid/Treble, P2 Gain/Master/Level, P3 Neve/Cabtype/Input.
  Removed: Voice (merged to Nashville character), Cab dark/bright blend
  (each cab owns its voicing), Presence knob (fixed 0.85). The old
  dark/bright chains also ran only 5 of their 6 biquads (the second
  4th-order-LP section was dropped); v17 chains run all 6, so the LP is
  genuinely 4th-order now.

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

## Parameters (9 knobs + Input trim + Voice, three pages)

| Page | Knob | Range | Default | Maps to |
|---|---|---|---|---|
| -   | Input | 0..1 | 1.00 | input trim: 0.125x..1.25x (1.0 = calibrated ref) |
| P1 | Bass | 0..1 | 0.50 | tone-network low, 140 Hz (0.5 flat) |
| P1 | Mid | 0..1 | 0.50 | tone-network mid presence, 850 Hz (0.5 flat) |
| P1 | Treble | 0..1 | 0.50 | tone-network high, 5 kHz (0.5 flat) |
| P2 | Gain | 0..1 | 0.35 | V2 cold-clipper drive (touch dynamics base, ~17x max) |
| P2 | Master | 0..1 | 0.55 | PI + push-pull power drive + sag amount |
| P2 | Level | 0..1 | 0.75 | output (0.20..0.90 gain) |
| P3 | Neve | 0..1 | 1.00 | Neve coloration wet/dry (0 = bypass) |
| P3 | Cab | 0..1 | 0.50 | cabinet voicing dark..bright |
| P3 | Presence | 0..1 | 0.85 | speaker 3.5 kHz resonance amount |
| V  | Voice | 0/1 | 0 | switch: 0 = Nashville, 1 = Emo/Edge |

The defaults are the "Edge / Breakup" factory preset - a balanced patch
that shows the touch dynamics (soft picks clean, hard picks crunch) with a
slightly tamed presence so the first note is never harsh.

## Presets / state memory

- **VST**: a preset row above the pedal buttons. Five factory presets
  (Nashville Clean, Edge / Breakup, British Crunch, High Gain, Emo / Edge)
  are hardcoded; user presets persist as XML files of the APVTS state in
  `<appdata>/AmpNeve/Presets` (SAVE overwrites the selected user preset or
  creates `Custom N.xml`, DEL removes it). Bypass is stripped from stored
  presets so loading one never comes back muted. The VST3 host state
  (getStateInformation/setStateInformation) is also implemented, so DAW
  session save/restore works independently of the in-plugin presets.
- **ZDL**: no preset system on purpose. The Zoom pedal stores every effect
  parameter inside the saved patch; the DSP reads `params[]` from the host
  every block, so saving the patch on the pedal is exactly "remembering
  the last state". No extra persistence code needed.

## ZDL-safe implementation notes

- Saturation curves: piecewise polynomials, different per stage (V1: tube
  polynomial + asymmetric clip_ts; V2: asymmetric clip_ts; power amp:
  clip_pp, an odd cubic soft clip with saturating tail). No tanh (no
  division / math lib).
- Envelope: `env += (|x| - env) * coeff` (one-pole, multiply-add).
- Tone network: peaking/shelf biquads with fixed center frequencies; the
  gain parameter is linear (0.5..2.0) so coefficients update with
  multiply-add only (no pow, no sin/cos at runtime - w0 is constant).
- Speaker resonance: fixed-coefficient biquads.
- State: ~4.7 KB per instance (most of it the 1024-tap IR delay buffer;
  two instances fit ctx[3] easily).

## Roadmap

- v1: multi-stage gain + touch dynamics + tone network + power sag +
  speaker resonance; 6-knob Gain/Bass/Mid/Treble/Master/Level.
- v2a: Nashville session voicing (see above) + cab lowpass bug fix.
- v2b: 9 knobs / three pages - Neve, Cab and Presence exposed as knobs.
- v2 (future): Voice 1..5 voicing presets (Clean American / British
  crunch / Dumble-ish / High-gain / Boutique clean), dual-mic cabinet
  (dynamic close + condenser room), presence/depth.

## v18b: gain-naturalness measurements (vs known-good gain ZDLs)

Measured with `tools/compare_gain.py` (native core render = same DSP code as
VST/ZDL; all artifacts in `out/gaincmp/`, `SUMMARY.txt` + `report.json`).
References: stock **BASSDRV** from the real firmware (C674x disassembly:
IIR + table soft-clip curve, no long FIR, no cab model) and **PurestDrive**
(exact algorithm port; community gain ZDL).

Result: no digital artifacts measurable.

- **Aliasing: none.** Folded products at -240 dB (noise floor) for 1k/4k/12k
  sines at max gain; DI guitar 19-22 kHz floor -104.8 dB. Every clip stage
  is followed by a lowpass (V1 9k, V2 5k, real cab IR rolloff), so there is
  no alias "fizz". PurestDrive measures -83 dB by comparison (no filtering).
- **DC: none.** Render means ~ -8e-7; impulse tail decays to -40 dB in
  16.7 ms - no low-frequency thump (DC blocks at 35 Hz + IR HP30).
- **Harmonics:** low gain h2 ~ h3 (-23/-21 dB, asymmetric tube character);
  max gain h3 dominant (-11 dB) with h2 down to -27 dB (push-pull odd-order
  character). 5th order and up roll off fast (5f1 -34.6 dB). IMD: 2nd order
  -5.6 dB, 5th order -34.6 dB - warm, no dense high-frequency IM clusters.
- **Compression:** g=1.0 1k sine: -40 -> -21, -24 -> -11, -12 -> -10.3,
  -3 -> -9.8 dBFS - smooth approach to a hard limit, like a loud tube power
  amp, no knee artifacts.
- **Response:** +7 dB low-mid body (200-700 Hz, real 2x12 behavior);
  -20 dB below 34 Hz; dark above 5k (-19 dB @ 5.8k) - the approved cab tone.
- **Tone knobs:** effective span 16-18 dB with pre-power-amp drive
  interaction (tone boost pushes the power stage into its soft-clip knee),
  consistent with real FMV stack behavior.
- **Knob zipper (only finding):** the ZDL reads params[] and updates
  coefficients every 8-sample block with no smoothing, so fast Gain/Level
  turns on the pedal can click (VST is smooth via JUCE ramping). Fix if
  heard on hardware: one-pole smoothing of v2_drive/level_gain in the audio
  loop (~4 instr, algorithm untouched).

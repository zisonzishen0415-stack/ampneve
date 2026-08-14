/* core/ampsim.c - AmpNeve boutique amp core (see ampsim.h). */
#include <stddef.h>   /* NULL - self-contained even when inlined (ZDL build) */

#include "ampsim.h"
#include "ampsim_coeffs.h"

typedef struct {
    float b0, b1, b2, a1, a2;   /* biquad coefficients (a0 normalized) */
    float v1, v2;               /* transposed direct-form-2 state */
} Bq;

typedef struct {
    /* params */
    float input;               /* input trim, 0..1 (1.0 = calibrated ref) */
    float gain, bass, mid, treble, master, level;
    float voice;               /* 0 = Nashville, 1 = Emo/Edge */
    float gain_base;           /* voice-dependent V2 drive base */
    float dry_mix;             /* Klon dry amount: more dry when clean */
    float sample_rate;

    /* input stage */
    float in_g;

    /* preamp stages: V1 fixed gain; V2 cold-clipper drive (touch dynamics) */
    float v2_drive;
    float env;
    float env_attack_c, env_release_c;
    float dc_x1, dc_y1;         /* input-stage DC block */
    Bq pre_lp1, pre_lp2;        /* Miller-cap interstage lowpasses (v15) */

    /* tone network (bass/mid/treble peaking, runtime linear-gain) */
    Bq tone_bass, tone_mid, tone_treble;

    /* power stage */
    float pi_drive;             /* phase-inverter drive */
    float pp_drive;             /* push-pull power-tube drive */
    float sag_amt;
    float sag_env;
    float sag_attack_c, sag_release_c;

    /* output transformer (Neve brand color) */
    float neve;                 /* coloration amount, 0..1 (0 = bypass) */
    float neve_g;
    float tr_x1, tr_y1;         /* transformer DC block */
    Bq neve_bq[3];

    /* speaker + cabinet */
    Bq reso_low, reso_high;
    Bq cab_dark[AMP_CAB_DARK_N];
    Bq cab_bright[AMP_CAB_BRIGHT_N];
    const float* ir;             /* active cabinet IR (static const) */
    float ir_delay[AMP_CAB_IR_N]; /* rolling convolution delay buffer */
    int ir_idx;                   /* write position into ir_delay */
    float cab_tone;             /* dark..bright blend (0..1) */
    float presence;             /* speaker 3.5kHz resonance amount, 0..1 */

    /* level */
    float level_gain;
} AmpsimState;

struct Ampsim {
    AmpsimState s;
};

uint32_t Ampsim_state_size(void) {
    return (uint32_t)sizeof(Ampsim);
}

static void bq_load(Bq* b, const AmpBiquad* c) {
    b->b0 = c->b0; b->b1 = c->b1; b->b2 = c->b2;
    b->a1 = c->a1; b->a2 = c->a2;
    b->v1 = 0.0f; b->v2 = 0.0f;
}

static float bq_run(Bq* b, float x) {
    float v = x - b->a1 * b->v1 - b->a2 * b->v2;
    float y = b->b0 * v + b->b1 * b->v1 + b->b2 * b->v2;
    b->v2 = b->v1;
    b->v1 = v;
    return y;
}

/* Fixed-point reciprocal approximation (3 Newton steps). Used only at
 * set_param time to build tone-network coefficients - the audio path has
 * no division at all. */
static float recip_approx(float x) {
    union { float f; unsigned int u; } conv;
    conv.f = x;
    conv.u = 0x7EF311C3u - conv.u;
    float y = conv.f;
    y = y * (2.0f - x * y);
    y = y * (2.0f - x * y);
    y = y * (2.0f - x * y);
    return y;
}

/* Input trim: 0..1 maps to 0.125x..1.25x input gain. The calibrated
 * reference (DI recorded at -12..-6 dBFS peak) is v = 1.0 -> 1.25x, the
 * historical fixed input gain. Pure multiply-add, no division. */
float Ampsim_input_gain(float v) {
    if (v < 0.0f) v = 0.0f;
    else if (v > 1.0f) v = 1.0f;
    return 0.125f + 1.125f * v;
}

/* tube-style asymmetric soft clip (Tube-Screamer family): the positive half
 * clips earlier (0.5) than the negative (0.8), injecting even harmonics so
 * the clipping is audible as grit, while C1 cubic knees keep high-order
 * harmonics low (warm, no digital buzz). Monotonic, ZDL-safe. */
static float clip_ts(float x) {
    float ax = x < 0.0f ? -x : x;
    float y;
    if (x >= 0.0f) {
        if (ax <= 0.50f)      y = ax;
        else if (ax <= 1.50f) {
            float t = ax - 0.50f;
            y = 0.50f + t - 0.30f * t * t * t;
        } else if (ax <= 3.50f) {
            y = 1.20f + 0.10f * (ax - 1.50f);
        } else                y = 1.40f;
    } else {
        if (ax <= 0.80f)      y = -ax;
        else if (ax <= 1.80f) {
            float t = ax - 0.80f;
            y = -(0.80f + t - 0.02f * t * t - 0.28f * t * t * t);
        } else if (ax <= 3.50f) {
            y = -(1.50f + 0.12f * (ax - 1.80f));
        } else                y = -1.70f;
    }
    return y;
}

/* Push-pull power-amp clip (v15): odd-symmetric, NFB-shaped. Push-pull
 * cancels even harmonics in the output transformer, so unlike the single-
 * ended preamp stages this curve is strictly odd (h2/h4/h6 ~ 0). The
 * classic cubic soft clip x - x^3/3 makes h3 the dominant harmonic (the
 * "power-amp breakup" sound), the tail then saturates gently to 1.08 with
 * no flat-top square (flat tops are what sound digital). Monotonic, C1,
 * ZDL-safe. Design verified in work/design_pp.py. */
static float clip_pp(float x) {
    float ax = x < 0.0f ? -x : x;
    float s  = x < 0.0f ? -1.0f : 1.0f;
    float y;
    if (ax <= 1.00f) {
        y = ax - ax * ax * ax * 0.333333333f;
    }
    else if (ax <= 2.30f) {
        float t = (ax - 1.00f) * 0.769231f;
        y = 0.666666667f + t * t * (1.130000000f + t * -0.736666667f);
    }
    else { y = 1.080000000f; }
    return s * y;
}

/* RBJ peaking biquad with linear gain A (0.5..1.5), fixed w0 constants.
 * Only multiply-add + recip_approx at set_param time. */
static void tone_peaking(Bq* b, float cosw, float alpha, float A) {
    float invA = recip_approx(A);
    float a0 = 1.0f + alpha * invA;
    float inv = recip_approx(a0);
    b->b0 = (1.0f + alpha * A) * inv;
    b->b1 = (-2.0f * cosw) * inv;
    b->b2 = (1.0f - alpha * A) * inv;
    b->a1 = (-2.0f * cosw) * inv;
    b->a2 = (1.0f - alpha * invA) * inv;
    b->v1 = b->v2 = 0.0f;
}

/* Reload the voice-dependent fixed stages (Neve EQ, cab voicing, mic
 * pickup). Tone-network coefficients are NOT touched - they follow the
 * Bass/Mid/Treble knobs and must survive a voice switch. */
static void update_voice_coeffs(Ampsim* a) {
    AmpsimState* s = &a->s;
    int i;
    int emo = s->voice >= 0.5f ? 1 : 0;
    for (i = 0; i < AMP_NEVE_N; ++i)
        bq_load(&s->neve_bq[i], emo ? &AMP_NEVE_EMO[i] : &AMP_NEVE[i]);
    for (i = 0; i < AMP_CAB_DARK_N; ++i)
        bq_load(&s->cab_dark[i], emo ? &AMP_CAB_DARK_EMO[i] : &AMP_CAB_DARK[i]);
    for (i = 0; i < AMP_CAB_BRIGHT_N; ++i)
        bq_load(&s->cab_bright[i], emo ? &AMP_CAB_BRIGHT_EMO[i] : &AMP_CAB_BRIGHT[i]);
    bq_load(&s->reso_low, &AMP_RESO_LOW);
    bq_load(&s->reso_high, &AMP_RESO_HIGH);
    s->ir = emo ? AMP_CAB_IR_EMO : AMP_CAB_IR_NASH;
}

static void update_stage_coeffs(Ampsim* a) {
    AmpsimState* s = &a->s;
    update_voice_coeffs(a);
    bq_load(&s->pre_lp1, &AMP_PRE_LP1);
    bq_load(&s->pre_lp2, &AMP_PRE_LP2);
    tone_peaking(&s->tone_bass,   AMP_TONE_BASS_COSW,   AMP_TONE_BASS_ALPHA,   1.0f);
    tone_peaking(&s->tone_mid,    AMP_TONE_MID_COSW,    AMP_TONE_MID_ALPHA,    1.0f);
    tone_peaking(&s->tone_treble, AMP_TONE_TREB_COSW,   AMP_TONE_TREB_ALPHA,   1.0f);
}

void Ampsim_reset(Ampsim* a) {
    if (!a) return;
    AmpsimState* s = &a->s;
    int i;
    for (i = 0; i < 3; ++i) { s->neve_bq[i].v1 = s->neve_bq[i].v2 = 0.0f; }
    for (i = 0; i < AMP_CAB_DARK_N; ++i) { s->cab_dark[i].v1 = s->cab_dark[i].v2 = 0.0f; }
    for (i = 0; i < AMP_CAB_BRIGHT_N; ++i) { s->cab_bright[i].v1 = s->cab_bright[i].v2 = 0.0f; }
    s->reso_low.v1 = s->reso_low.v2 = 0.0f;
    s->reso_high.v1 = s->reso_high.v2 = 0.0f;
    s->pre_lp1.v1 = s->pre_lp1.v2 = 0.0f;
    s->pre_lp2.v1 = s->pre_lp2.v2 = 0.0f;
    for (i = 0; i < AMP_CAB_IR_N; ++i) s->ir_delay[i] = 0.0f;
    s->ir_idx = 0;
    s->ir = (s->voice >= 0.5f) ? AMP_CAB_IR_EMO : AMP_CAB_IR_NASH;
    s->tone_bass.v1 = s->tone_bass.v2 = 0.0f;
    s->tone_mid.v1 = s->tone_mid.v2 = 0.0f;
    s->tone_treble.v1 = s->tone_treble.v2 = 0.0f;
    s->env = 0.0f;
    s->sag_env = 0.0f;
    s->dc_x1 = s->dc_y1 = 0.0f;
    s->tr_x1 = s->tr_y1 = 0.0f;
}

Ampsim* Ampsim_init(void* mem, uint32_t bytes, float sample_rate) {
    if (!mem || bytes < Ampsim_state_size()) return NULL;
    if (sample_rate < 8000.0f) sample_rate = 44100.0f;
    Ampsim* a = (Ampsim*)mem;
    AmpsimState* s = &a->s;
    s->sample_rate = sample_rate;

    s->input = 1.0f;
    s->voice = 0.0f;
    s->gain_base = 0.2f;
    s->gain = 0.35f;
    s->dry_mix = 0.40f - 0.17f * s->gain;
    s->bass = s->mid = s->treble = 0.50f;
    s->master = 0.55f;
    s->level = 0.75f;
    s->neve = 1.0f;
    s->cab_tone = 0.5f;
    s->presence = 0.85f;

    s->in_g = Ampsim_input_gain(s->input);
    /* V2 cold-clipper drive: moderate at low knob (edge-of-breakup at the
     * 0.25 default), ~15.5x at max -> ~62x preamp total with V1's fixed 4x.
     * A real preamp stacks moderate per-stage gain instead of one giant
     * gain stage, and each stage's Miller LP shapes its harmonics. */
    s->v2_drive = s->gain_base + 1.5f * s->gain + 16.0f * s->gain * s->gain;
    s->pi_drive = 0.35f + 0.65f * s->master;
    s->pp_drive = 0.30f + 0.75f * s->master;
    s->sag_amt = 0.24f * s->master;
    s->neve_g = 1.2f;
    s->level_gain = 0.20f + 0.70f * s->level;

    /* one-pole time constants (approx tau = 1/(fs*c)); no division */
    s->env_attack_c  = recip_approx(sample_rate * 0.002f);  /* ~2 ms touch */
    s->env_release_c = recip_approx(sample_rate * 0.060f);  /* ~60 ms, no pump */
    s->sag_attack_c  = recip_approx(sample_rate * 0.001f);  /* ~1 ms */
    s->sag_release_c = recip_approx(sample_rate * 0.200f);  /* ~200 ms */

    update_stage_coeffs(a);
    Ampsim_reset(a);
    return a;
}

void Ampsim_set_param(Ampsim* a, AmpsimParam p, float v) {
    if (!a) return;
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    AmpsimState* s = &a->s;
    /* if-else chain, NOT switch: cl6x lowers a switch to a $C$SW jump
     * table in a .switch: section, and the reverson ZDL linker does not
     * place that section - the table relocations are skipped and knob
     * turns would jump into garbage on the pedal. */
    if (p == AMP_PARAM_INPUT) {
        s->input = v;
        s->in_g = Ampsim_input_gain(v);
    } else if (p == AMP_PARAM_GAIN) {
        s->gain = v;
        s->v2_drive = s->gain_base + 1.5f * v + 16.0f * v * v;
        s->dry_mix = 0.40f - 0.17f * v;
    } else if (p == AMP_PARAM_VOICE) {
        float nv = (v >= 0.5f) ? 1.0f : 0.0f;
        if (nv != s->voice) {
            s->voice = nv;
            s->gain_base = (nv >= 0.5f) ? 0.35f : 0.2f;
            s->v2_drive = s->gain_base + 1.5f * s->gain + 16.0f * s->gain * s->gain;
            s->dry_mix = 0.40f - 0.17f * s->gain;
            update_voice_coeffs(a);
        }
    } else if (p == AMP_PARAM_BASS) {
        if (v != s->bass) {
            s->bass = v;
            tone_peaking(&s->tone_bass, AMP_TONE_BASS_COSW, AMP_TONE_BASS_ALPHA, 0.5f + v);
        }
    } else if (p == AMP_PARAM_MID) {
        if (v != s->mid) {
            s->mid = v;
            tone_peaking(&s->tone_mid, AMP_TONE_MID_COSW, AMP_TONE_MID_ALPHA, 0.5f + v);
        }
    } else if (p == AMP_PARAM_TREBLE) {
        if (v != s->treble) {
            s->treble = v;
            tone_peaking(&s->tone_treble, AMP_TONE_TREB_COSW, AMP_TONE_TREB_ALPHA, 0.5f + v);
        }
    } else if (p == AMP_PARAM_MASTER) {
        s->master = v;
        s->pi_drive = 0.35f + 0.65f * v;
        s->pp_drive = 0.30f + 0.75f * v;
        s->sag_amt = 0.24f * v;
    } else if (p == AMP_PARAM_LEVEL) {
        s->level = v;
        s->level_gain = 0.20f + 0.70f * v;
    } else if (p == AMP_PARAM_NEVE) {
        s->neve = v;
    } else if (p == AMP_PARAM_CAB) {
        s->cab_tone = v;
    } else if (p == AMP_PARAM_PRESENCE) {
        s->presence = v;
    }
}

float Ampsim_get_param(const Ampsim* a, AmpsimParam p) {
    if (!a) return 0.0f;
    const AmpsimState* s = &a->s;
    /* if-else chain: same $C$SW jump-table rule as Ampsim_set_param. */
    if (p == AMP_PARAM_INPUT)          return s->input;
    else if (p == AMP_PARAM_GAIN)      return s->gain;
    else if (p == AMP_PARAM_BASS)      return s->bass;
    else if (p == AMP_PARAM_MID)       return s->mid;
    else if (p == AMP_PARAM_TREBLE)    return s->treble;
    else if (p == AMP_PARAM_MASTER)    return s->master;
    else if (p == AMP_PARAM_LEVEL)     return s->level;
    else if (p == AMP_PARAM_NEVE)      return s->neve;
    else if (p == AMP_PARAM_CAB)       return s->cab_tone;
    else if (p == AMP_PARAM_PRESENCE)  return s->presence;
    else if (p == AMP_PARAM_VOICE)     return s->voice;
    return 0.0f;
}

void Ampsim_process(Ampsim* a, float in, float* out) {
    if (!a || !out) return;
    AmpsimState* s = &a->s;
    int i;

    /* 1. V1 input stage: fixed ~4x gain with a light tube polynomial
     *    (even-harmonic bloom) and a soft asymmetric clip - the first tube
     *    stage CAN clip on a hot DI, exactly like a real amp's V1. The
     *    Miller-cap lowpass @ 9 kHz then shapes the harmonics this stage
     *    generates before V2 multiplies them. */
    float x = in * s->in_g;
    {
        float x2 = x * x;
        x = (x - 0.05f * x2 * x + 0.015f * x2) * 4.0f;
        x = clip_ts(x * 0.25f) * 4.0f;
        x = bq_run(&s->pre_lp1, x);
    }
    /* clean tap for the Klon-style mix: the signal that only went through
     * V1 (it will also pass the tone stack and power amp below) - a clean
     * amp tone, NOT the raw guitar DI. */
    float clean = x;

    /* 2. V2 cold clipper (the Gain knob): touch-dynamics envelope rides
     *    the V1 signal (soft picks stay clean, hard picks break up), then
     *    the JCM800-style asymmetric clip, then the second Miller LP
     *    @ 5 kHz - this is the stage that shapes the 3-6 kHz harmonic
     *    cluster (the "modern/Friedman" push) before the tone stack can
     *    boost it, and rolls the fizz off above. */
    {
        float ax = x < 0.0f ? -x : x;
        float c = (ax > s->env) ? s->env_attack_c : s->env_release_c;
        s->env += (ax - s->env) * c;
        float drive = s->v2_drive * (0.55f + 0.55f * s->env);
        x = clip_ts(x * drive);
        x = bq_run(&s->pre_lp2, x);
    }

    /* 3. Klon-style parallel mix, sample-aligned by construction: the V1
     *    clean tap and the V2 driven path sum BEFORE the tone network, so
     *    both branches pass through the same tone stack, power amp and
     *    cab - the "dry" is a clean amp tone through the power stage, not
     *    a raw guitar. More clean at low gain (pick attack), distortion
     *    dominates at max. */
    x = clean * s->dry_mix + x * (1.0f - s->dry_mix);

    /* 4. DC block: the asymmetric clips inject DC (the x^2 polynomial and
     *    the 0.5/0.8 knee asymmetry both do); one pole after the mix. */
    {
        float dy = x - s->dc_x1 + 0.995f * s->dc_y1;
        s->dc_x1 = x;
        s->dc_y1 = dy;
        x = dy;
    }

    /* 5. tone network (interacting bass/mid/treble) - FMV position: after
     *    the preamp stages, before the power amp. */
    x = bq_run(&s->tone_bass, x);
    x = bq_run(&s->tone_mid, x);
    x = bq_run(&s->tone_treble, x);

    /* 6. power amp: phase inverter (asymmetric LTP clip) then push-pull
     *    power tubes (odd-symmetric, NFB-shaped clip) + sag (envelope dips
     *    the drive on transients - the tube-rectifier "give"). */
    {
        float ax = x < 0.0f ? -x : x;
        float c = (ax > s->sag_env) ? s->sag_attack_c : s->sag_release_c;
        s->sag_env += (ax - s->sag_env) * c;
        float sag = 1.0f - s->sag_amt * s->sag_env;
        x = clip_ts(x * s->pi_drive);
        x = clip_pp(x * s->pp_drive * sag);
    }

    /* 7. output transformer: Neve even harmonics + 1073 EQ (brand color).
     *    The Neve knob is a wet/dry blend around the whole stage. */
    {
        float dry = x;
        float g = s->neve_g;
        float c1 = clip_ts(x * g);
        float c2 = clip_ts(x * g * 1.6f);
        float sat = c1 + 0.15f * c2 * c2;          /* asymmetric */
        float dy = sat - s->tr_x1 + 0.995f * s->tr_y1;   /* DC block */
        s->tr_x1 = sat;
        s->tr_y1 = dy;
        float y = dy;
        for (i = 0; i < 3; ++i) y = bq_run(&s->neve_bq[i], y);
        x = dry * (1.0f - s->neve) + (y * 0.9f) * s->neve;  /* 1073 trim in wet */
    }

    /* 8. speaker resonance + cabinet voicing */
    x = bq_run(&s->reso_low, x);
    {
        float dry = x;
        float wet = bq_run(&s->reso_high, dry);
        x = dry * (1.0f - s->presence) + wet * s->presence;
    }
    {
        float dark = x, bright = x;
        for (i = 0; i < AMP_CAB_DARK_N; ++i) dark = bq_run(&s->cab_dark[i], dark);
        for (i = 0; i < AMP_CAB_BRIGHT_N; ++i) bright = bq_run(&s->cab_bright[i], bright);
        x = dark * (1.0f - s->cab_tone) + bright * s->cab_tone;
    }

    /* 9. cabinet IR convolution (static 1024-tap miked-cab kernel: mic
     * pickup + speaker-cone resonances + room).  This is the link that
     * makes it read as a miked cab instead of an EQ'd DI - real cones
     * ring and real mics hear a room, and convolution carries that time
     * structure.  No division, no modulo: ring-buffer wrap by branch. */
    {
        int idx = s->ir_idx;
        float acc = 0.0f;
        s->ir_delay[idx] = x;
        for (i = 0; i < AMP_CAB_IR_N; ++i) {
            acc += s->ir_delay[idx] * s->ir[i];
            idx = (idx == 0) ? (AMP_CAB_IR_N - 1) : (idx - 1);
        }
        s->ir_idx = (s->ir_idx == (AMP_CAB_IR_N - 1)) ? 0 : (s->ir_idx + 1);
        x = acc;
    }

    /* 10. level */
    *out = x * s->level_gain;
}

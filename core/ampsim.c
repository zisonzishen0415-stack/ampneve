/* core/ampsim.c - AmpNeve boutique amp core (see ampsim.h). */
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
    float gain_base;           /* voice-dependent gain-stage base */
    float sample_rate;

    /* input stage */
    float in_g;

    /* gain stage (touch dynamics) */
    float gain_drive;
    float env;
    float env_attack_c, env_release_c;
    float dc_x1, dc_y1;         /* input-stage DC block */

    /* tone network (bass/mid/treble peaking, runtime linear-gain) */
    Bq tone_bass, tone_mid, tone_treble;

    /* power stage */
    float power_drive;
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
    Bq mic[AMP_MIC_N];          /* SM57-style mic pickup (fixed) */
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

/* smooth cubic soft clip: x - x^3/3 on [-1,1], saturates at +-2/3.
 * No division, no math library. */
static float clip3(float x) {
    if (x > 1.0f)  return 0.6666667f;
    if (x < -1.0f) return -0.6666667f;
    return x - 0.33333334f * x * x * x;
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
    for (i = 0; i < AMP_MIC_N; ++i)
        bq_load(&s->mic[i], emo ? &AMP_MIC_EMO[i] : &AMP_MIC[i]);
}

static void update_stage_coeffs(Ampsim* a) {
    AmpsimState* s = &a->s;
    update_voice_coeffs(a);
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
    for (i = 0; i < AMP_MIC_N; ++i) { s->mic[i].v1 = s->mic[i].v2 = 0.0f; }
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
    s->gain = 0.45f;
    s->bass = s->mid = s->treble = 0.50f;
    s->master = 0.50f;
    s->level = 0.80f;
    s->neve = 1.0f;
    s->cab_tone = 0.5f;
    s->presence = 1.0f;

    s->in_g = Ampsim_input_gain(s->input);
    s->gain_drive = s->gain_base + 2.6f * s->gain;
    s->power_drive = 0.5f + 1.3f * s->master;
    s->sag_amt = 0.30f * s->master;
    s->neve_g = 1.9f;
    s->level_gain = 0.5f + s->level;

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
    switch (p) {
        case AMP_PARAM_INPUT:
            s->input = v;
            s->in_g = Ampsim_input_gain(v);
            break;
        case AMP_PARAM_GAIN:
            s->gain = v;
            s->gain_drive = s->gain_base + 2.6f * v;
            break;
        case AMP_PARAM_VOICE:
        {
            float nv = (v >= 0.5f) ? 1.0f : 0.0f;
            if (nv != s->voice) {
                s->voice = nv;
                s->gain_base = (nv >= 0.5f) ? 0.35f : 0.2f;
                s->gain_drive = s->gain_base + 2.6f * s->gain;
                update_voice_coeffs(a);
            }
            break;
        }
        case AMP_PARAM_BASS:
            s->bass = v;
            tone_peaking(&s->tone_bass, AMP_TONE_BASS_COSW, AMP_TONE_BASS_ALPHA, 0.5f + v);
            break;
        case AMP_PARAM_MID:
            s->mid = v;
            tone_peaking(&s->tone_mid, AMP_TONE_MID_COSW, AMP_TONE_MID_ALPHA, 0.5f + v);
            break;
        case AMP_PARAM_TREBLE:
            s->treble = v;
            tone_peaking(&s->tone_treble, AMP_TONE_TREB_COSW, AMP_TONE_TREB_ALPHA, 0.5f + v);
            break;
        case AMP_PARAM_MASTER:
            s->master = v;
            s->power_drive = 0.5f + 1.3f * v;
            s->sag_amt = 0.30f * v;
            break;
        case AMP_PARAM_LEVEL:
            s->level = v;
            s->level_gain = 0.5f + v;
            break;
        case AMP_PARAM_NEVE:
            s->neve = v;
            break;
        case AMP_PARAM_CAB:
            s->cab_tone = v;
            break;
        case AMP_PARAM_PRESENCE:
            s->presence = v;
            break;
    }
}

float Ampsim_get_param(const Ampsim* a, AmpsimParam p) {
    if (!a) return 0.0f;
    const AmpsimState* s = &a->s;
    switch (p) {
        case AMP_PARAM_INPUT:  return s->input;
        case AMP_PARAM_GAIN:   return s->gain;
        case AMP_PARAM_BASS:   return s->bass;
        case AMP_PARAM_MID:    return s->mid;
        case AMP_PARAM_TREBLE: return s->treble;
        case AMP_PARAM_MASTER: return s->master;
        case AMP_PARAM_LEVEL:  return s->level;
        case AMP_PARAM_NEVE:    return s->neve;
        case AMP_PARAM_CAB:     return s->cab_tone;
        case AMP_PARAM_PRESENCE: return s->presence;
        case AMP_PARAM_VOICE:   return s->voice;
    }
    return 0.0f;
}

void Ampsim_process(Ampsim* a, float in, float* out) {
    if (!a || !out) return;
    AmpsimState* s = &a->s;
    int i;

    /* 1. input stage: light asymmetric saturation (even + odd harmonics),
     *    then a DC block (the x^2 term injects DC). */
    float x = in * s->in_g;
    float x2 = x * x;
    x = x - 0.07f * x2 * x + 0.02f * x2;
    {
        float dy = x - s->dc_x1 + 0.995f * s->dc_y1;
        s->dc_x1 = x;
        s->dc_y1 = dy;
        x = dy;
    }

    /* 2. gain stage with touch dynamics: the clip drive rides the input
     *    envelope, so soft picks stay clean and hard picks break up. */
    {
        float ax = x < 0.0f ? -x : x;
        float c = (ax > s->env) ? s->env_attack_c : s->env_release_c;
        s->env += (ax - s->env) * c;
        float drive = s->gain_drive * (0.65f + 0.45f * s->env);
        x = clip3(x * drive);
    }

    /* 3. tone network (interacting bass/mid/treble) */
    x = bq_run(&s->tone_bass, x);
    x = bq_run(&s->tone_mid, x);
    x = bq_run(&s->tone_treble, x);

    /* 4. power stage: drive + sag (envelope dips gain on transients) */
    {
        float ax = x < 0.0f ? -x : x;
        float c = (ax > s->sag_env) ? s->sag_attack_c : s->sag_release_c;
        s->sag_env += (ax - s->sag_env) * c;
        float sag = 1.0f - s->sag_amt * s->sag_env;
        x = clip3(x * s->power_drive * sag);
    }

    /* 5. output transformer: Neve even harmonics + 1073 EQ (brand color).
     *    The Neve knob is a wet/dry blend around the whole stage. */
    {
        float dry = x;
        float g = s->neve_g;
        float c1 = clip3(x * g);
        float c2 = clip3(x * g * 1.6f);
        float sat = c1 + 0.15f * c2 * c2;          /* asymmetric */
        float dy = sat - s->tr_x1 + 0.995f * s->tr_y1;   /* DC block */
        s->tr_x1 = sat;
        s->tr_y1 = dy;
        float y = dy;
        for (i = 0; i < 3; ++i) y = bq_run(&s->neve_bq[i], y);
        x = dry * (1.0f - s->neve) + (y * 0.9f) * s->neve;  /* 1073 trim in wet */
    }

    /* 6. speaker resonance + cabinet voicing */
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

    /* 7. mic pickup (SM57-ish, fixed): the last link that makes it read
     * as a miked cab instead of an EQ'd DI. */
    for (i = 0; i < AMP_MIC_N; ++i) x = bq_run(&s->mic[i], x);

    /* 8. level */
    *out = x * s->level_gain;
}

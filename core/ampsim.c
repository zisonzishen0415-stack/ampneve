/* core/ampsim.c - AmpNeve core (see ampsim.h). */
#include "ampsim.h"
#include "ampsim_coeffs.h"

typedef struct {
    float b0, b1, b2, a1, a2;   /* biquad coefficients (a0 normalized) */
    float v1, v2;               /* transposed direct-form-2 state */
} Bq;

typedef struct {
    /* params */
    float drive, tone, level, bass, neve, cab;
    float sample_rate;

    /* soft clip stage */
    float clip_g;               /* pre-gain into the clipper (1 + 3*drive) */
    float clip_k;               /* dry/wet blend for drive */

    /* neve stage: 3 biquads + dc block */
    Bq neve_bq[3];
    float dc_x1, dc_y1;         /* one-pole DC block */
    float neve_g;               /* saturation pre-gain (fixed 1.9) */

    /* cab stage: dark chain (4 bq), bright chain (4 bq), bass bq */
    Bq cab_dark[4];
    Bq cab_bright[4];
    Bq bass_bq;

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
    float v  = x - b->a1 * b->v1 - b->a2 * b->v2;
    float y  = b->b0 * v + b->b1 * b->v1 + b->b2 * b->v2;
    b->v2 = b->v1;
    b->v1 = v;
    return y;
}

/* smooth cubic soft clip: x - x^3/3 on [-1,1], saturates at +-2/3.
 * No division, no math library. */
static float clip3(float x) {
    if (x > 1.0f)  return 0.6666667f;
    if (x < -1.0f) return -0.6666667f;
    return x - 0.33333334f * x * x * x;
}

static void update_stage_coeffs(Ampsim* a) {
    AmpsimState* s = &a->s;
    int i;
    for (i = 0; i < 3; ++i) bq_load(&s->neve_bq[i], &AMP_NEVE[i]);
    for (i = 0; i < 4; ++i) bq_load(&s->cab_dark[i], &AMP_CAB_DARK[i]);
    for (i = 0; i < 4; ++i) bq_load(&s->cab_bright[i], &AMP_CAB_BRIGHT[i]);
    bq_load(&s->bass_bq, &AMP_BASS);
}

void Ampsim_reset(Ampsim* a) {
    if (!a) return;
    AmpsimState* s = &a->s;
    int i;
    for (i = 0; i < 3; ++i) { s->neve_bq[i].v1 = s->neve_bq[i].v2 = 0.0f; }
    for (i = 0; i < 4; ++i) {
        s->cab_dark[i].v1 = s->cab_dark[i].v2 = 0.0f;
        s->cab_bright[i].v1 = s->cab_bright[i].v2 = 0.0f;
    }
    s->bass_bq.v1 = s->bass_bq.v2 = 0.0f;
    s->dc_x1 = s->dc_y1 = 0.0f;
}

Ampsim* Ampsim_init(void* mem, uint32_t bytes, float sample_rate) {
    if (!mem || bytes < Ampsim_state_size()) return NULL;
    if (sample_rate < 8000.0f) sample_rate = 44100.0f;
    Ampsim* a = (Ampsim*)mem;
    a->s.sample_rate = sample_rate;
    a->s.drive = 0.4f;      /* default: light drive */
    a->s.tone  = 0.5f;
    a->s.level = 0.8f;
    a->s.bass  = 0.5f;
    a->s.neve  = 0.6f;
    a->s.cab   = 1.0f;
    a->s.clip_g = 1.0f + 3.0f * a->s.drive;
    a->s.clip_k = 0.5f + 0.5f * a->s.drive;
    a->s.neve_g = 1.9f;
    a->s.level_gain = 0.5f + a->s.level;   /* 0.5..1.5 */
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
        case AMP_PARAM_DRIVE:
            s->drive = v;
            s->clip_g = 1.0f + 3.0f * v;
            s->clip_k = 0.5f + 0.5f * v;
            break;
        case AMP_PARAM_TONE:  s->tone = v; break;
        case AMP_PARAM_LEVEL:
            s->level = v;
            s->level_gain = 0.5f + v;
            break;
        case AMP_PARAM_BASS:  s->bass = v; break;
        case AMP_PARAM_NEVE:  s->neve = v; break;
        case AMP_PARAM_CAB:   s->cab = v; break;
    }
}

float Ampsim_get_param(const Ampsim* a, AmpsimParam p) {
    if (!a) return 0.0f;
    const AmpsimState* s = &a->s;
    switch (p) {
        case AMP_PARAM_DRIVE: return s->drive;
        case AMP_PARAM_TONE:  return s->tone;
        case AMP_PARAM_LEVEL: return s->level;
        case AMP_PARAM_BASS:  return s->bass;
        case AMP_PARAM_NEVE:  return s->neve;
        case AMP_PARAM_CAB:   return s->cab;
    }
    return 0.0f;
}

void Ampsim_process(Ampsim* a, float in, float* out) {
    if (!a || !out) return;
    AmpsimState* s = &a->s;
    int i;

    /* 1. soft clip (drive): blend dry in with the clipped signal */
    float d = s->drive;
    float sc = clip3(in * s->clip_g);
    float x = in * (1.0f - d) + sc * d;

    /* 2. Neve coloration: transformer-style even harmonics + 1073 EQ,
     *      blended by neve amount */
    float n = s->neve;
    if (n > 0.0f) {
        float g = s->neve_g;
        float c1 = clip3(x * g);
        float c2 = clip3(x * g * 1.6f);
        float sat = c1 + 0.15f * c2 * c2;          /* asymmetric (even harmonics) */
        /* DC block (one-pole HPF ~30 Hz) */
        float dcy = sat - s->dc_x1 + 0.995f * s->dc_y1;
        s->dc_x1 = sat;
        s->dc_y1 = dcy;
        float tone = dcy;
        for (i = 0; i < 3; ++i) tone = bq_run(&s->neve_bq[i], tone);
        x = x * (1.0f - n) + tone * n;
    }

    /* 3. cabinet voicing: dark..bright chains, bass body, dry/wet blend */
    float c = s->cab;
    if (c > 0.0f) {
        float dark = x, bright = x;
        for (i = 0; i < 4; ++i) dark = bq_run(&s->cab_dark[i], dark);
        for (i = 0; i < 4; ++i) bright = bq_run(&s->cab_bright[i], bright);
        float t = s->tone;
        float voiced = dark * (1.0f - t) + bright * t;   /* dark..bright */
        float b = s->bass;
        if (b > 0.0f) {
            float body = bq_run(&s->bass_bq, voiced);
            voiced = voiced * (1.0f - b) + body * b;
        }
        x = x * (1.0f - c) + voiced * c;
    }

    /* 4. level */
    *out = x * s->level_gain;
}

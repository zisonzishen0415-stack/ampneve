/* tests/test_ampsim.c - numeric behavior + stability for the AmpNeve core. */
#include "ampsim.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (line %d)\n", msg, __LINE__); ++failures; } \
    else { printf("ok:   %s\n", msg); } \
} while (0)

static float run_rms(Ampsim* a, float freq, float amp, unsigned n, unsigned skip) {
    float sum2 = 0.0f;
    unsigned cnt = 0;
    for (unsigned i = 0; i < n; ++i) {
        float t = (float)i / 44100.0f;
        float in = amp * sinf(2.0f * 3.14159265f * freq * t);
        float o = 0.0f;
        Ampsim_process(a, in, &o);
        if (i >= skip) { sum2 += o * o; ++cnt; }
    }
    return cnt > 0u ? sqrtf(sum2 / (float)cnt) : 0.0f;
}

int main(void) {
    uint32_t need = Ampsim_state_size();
    printf("state_size = %u bytes\n", (unsigned)need);
    CHECK(need > 0 && need < 65536u, "state size sane");

    size_t nf = (need + sizeof(float) - 1u) / sizeof(float);
    float* mem = (float*)calloc(nf, sizeof(float));
    CHECK(mem != NULL, "alloc scratch");
    if (!mem) return 1;

    Ampsim* a = Ampsim_init(mem, (uint32_t)(nf * sizeof(float)), 44100.0f);
    CHECK(a != NULL, "init ok");

    /* default params in range */
    {
        int ok = 1;
        int p;
        for (p = 0; p < (int)AMPNEVE_NUM_PARAMS; ++p) {
            float v = Ampsim_get_param(a, (AmpsimParam)p);
            if (!(v >= 0.0f && v <= 1.0f)) ok = 0;
        }
        CHECK(ok, "all defaults in 0..1");
    }

    /* 1. soft clip bounds: big input with full gain+master stays bounded */
    Ampsim_set_param(a, AMP_PARAM_GAIN, 1.0f);
    Ampsim_set_param(a, AMP_PARAM_MASTER, 1.0f);
    Ampsim_set_param(a, AMP_PARAM_LEVEL, 0.0f);
    Ampsim_reset(a);
    float o = 0.0f, peak = 0.0f;
    for (int i = 0; i < 4096; ++i) {
        Ampsim_process(a, 8.0f, &o);
        if (fabsf(o) > peak) peak = fabsf(o);
    }
    CHECK(peak > 0.0f && peak < 1.0f, "soft clip bounded");

    /* 2. no NaN/Inf over a long sine run */
    Ampsim_set_param(a, AMP_PARAM_GAIN, 0.7f);
    Ampsim_set_param(a, AMP_PARAM_BASS, 0.6f);
    Ampsim_set_param(a, AMP_PARAM_MID, 0.4f);
    Ampsim_set_param(a, AMP_PARAM_TREBLE, 0.6f);
    Ampsim_set_param(a, AMP_PARAM_MASTER, 0.7f);
    Ampsim_set_param(a, AMP_PARAM_LEVEL, 1.0f);
    Ampsim_reset(a);
    {
        int bad = 0;
        double sum2 = 0.0;
        unsigned cnt = 0;
        for (unsigned i = 0; i < 200000u; ++i) {
            float t = (float)i / 44100.0f;
            float in = 0.5f * sinf(2.0f * 3.14159265f * 220.0f * t)
                     + 0.25f * sinf(2.0f * 3.14159265f * 660.0f * t);
            Ampsim_process(a, in, &o);
            if (!(o == o) || o > 1e6f || o < -1e6f) { bad = 1; break; }
            sum2 += (double)o * o;
            ++cnt;
        }
        CHECK(!bad, "stable, no NaN/Inf on sine run");
        CHECK(cnt > 0u && sum2 > 0.0, "output has energy");
    }

    /* 3. param clamping */
    Ampsim_set_param(a, AMP_PARAM_GAIN, 5.0f);
    CHECK(Ampsim_get_param(a, AMP_PARAM_GAIN) == 1.0f, "clamps high");
    Ampsim_set_param(a, AMP_PARAM_GAIN, -1.0f);
    CHECK(Ampsim_get_param(a, AMP_PARAM_GAIN) == 0.0f, "clamps low");

    /* 4. reset deterministic */
    Ampsim_set_param(a, AMP_PARAM_GAIN, 0.5f);
    Ampsim_set_param(a, AMP_PARAM_BASS, 0.5f);
    Ampsim_set_param(a, AMP_PARAM_MID, 0.5f);
    Ampsim_set_param(a, AMP_PARAM_TREBLE, 0.5f);
    Ampsim_set_param(a, AMP_PARAM_MASTER, 0.5f);
    Ampsim_set_param(a, AMP_PARAM_LEVEL, 0.8f);
    Ampsim_reset(a);
    Ampsim_process(a, 0.1f, &o);
    float after1 = o;
    Ampsim_reset(a);
    Ampsim_process(a, 0.1f, &o);
    CHECK(fabsf(o - after1) < 1e-5f, "reset deterministic");

    /* 5. tone network responds: bass boost vs cut at 80 Hz */
    {
        Ampsim_set_param(a, AMP_PARAM_GAIN, 0.4f);
        Ampsim_set_param(a, AMP_PARAM_MID, 0.5f);
        Ampsim_set_param(a, AMP_PARAM_TREBLE, 0.5f);
        Ampsim_set_param(a, AMP_PARAM_MASTER, 0.5f);
        Ampsim_set_param(a, AMP_PARAM_LEVEL, 0.8f);
        Ampsim_set_param(a, AMP_PARAM_BASS, 1.0f);
        Ampsim_reset(a);
        float r_bass_hi = run_rms(a, 120.0f, 0.12f, 32768u, 16384u);
        Ampsim_set_param(a, AMP_PARAM_BASS, 0.0f);
        Ampsim_reset(a);
        float r_bass_lo = run_rms(a, 120.0f, 0.12f, 32768u, 16384u);
        CHECK(r_bass_hi > 1.15f * r_bass_lo, "bass knob boosts low end");
    }

    /* 6a. tone network: mid boost vs cut at 850 Hz */
    {
        Ampsim_set_param(a, AMP_PARAM_BASS, 0.5f);
        Ampsim_set_param(a, AMP_PARAM_MID, 1.0f);
        Ampsim_set_param(a, AMP_PARAM_TREBLE, 0.5f);
        Ampsim_reset(a);
        float r_mid_hi = run_rms(a, 850.0f, 0.12f, 32768u, 16384u);
        Ampsim_set_param(a, AMP_PARAM_MID, 0.0f);
        Ampsim_reset(a);
        float r_mid_lo = run_rms(a, 850.0f, 0.12f, 32768u, 16384u);
        CHECK(r_mid_hi > 1.15f * r_mid_lo, "mid knob boosts presence");
    }

    /* 6. tone network: treble boost vs cut at 5 kHz */
    {
        Ampsim_set_param(a, AMP_PARAM_BASS, 0.5f);
        Ampsim_set_param(a, AMP_PARAM_MID, 0.5f);
        Ampsim_set_param(a, AMP_PARAM_TREBLE, 1.0f);
        Ampsim_reset(a);
        float r_treb_hi = run_rms(a, 5000.0f, 0.06f, 32768u, 16384u);
        Ampsim_set_param(a, AMP_PARAM_TREBLE, 0.0f);
        Ampsim_reset(a);
        float r_treb_lo = run_rms(a, 5000.0f, 0.06f, 32768u, 16384u);
        CHECK(r_treb_hi > 1.15f * r_treb_lo, "treble knob boosts high end");
    }

    /* 7. level scales output */
    {
        Ampsim_set_param(a, AMP_PARAM_GAIN, 0.4f);
        Ampsim_set_param(a, AMP_PARAM_BASS, 0.5f);
        Ampsim_set_param(a, AMP_PARAM_MID, 0.5f);
        Ampsim_set_param(a, AMP_PARAM_TREBLE, 0.5f);
        Ampsim_set_param(a, AMP_PARAM_MASTER, 0.5f);
        Ampsim_set_param(a, AMP_PARAM_LEVEL, 1.0f);
        Ampsim_reset(a);
        float r_lv_hi = run_rms(a, 220.0f, 0.15f, 16384u, 8192u);
        Ampsim_set_param(a, AMP_PARAM_LEVEL, 0.0f);
        Ampsim_reset(a);
        float r_lv_lo = run_rms(a, 220.0f, 0.15f, 16384u, 8192u);
        CHECK(r_lv_hi > 2.0f * r_lv_lo, "level scales output");
    }

    /* 8. dynamic compression: loud input compresses vs quiet input.
     *    v7 raised the power-stage drive (0.5 + 2.2*master) and sag to make
     *    the tube power amp saturate harder, so cranked settings now squash
     *    hard (ratio near 1.0). At edge-of-breakup the amp must still keep
     *    pick dynamics - that is the touch-dynamics promise.
     *    The lower bound is a sanity check only (loud stays louder): the
     *    ratio is measured post-cab, so IR retunes shift it by ~0.001 (the
     *    v16 IR moved it 1.030 -> 1.029). The anti-regression meaning is the
     *    UPPER bound: 2.9 vs the 3.33 a linear chain would give. */
    {
        Ampsim_set_param(a, AMP_PARAM_GAIN, 0.8f);
        Ampsim_set_param(a, AMP_PARAM_BASS, 0.5f);
        Ampsim_set_param(a, AMP_PARAM_MID, 0.5f);
        Ampsim_set_param(a, AMP_PARAM_TREBLE, 0.5f);
        Ampsim_set_param(a, AMP_PARAM_MASTER, 0.8f);
        Ampsim_set_param(a, AMP_PARAM_LEVEL, 0.8f);
        Ampsim_set_param(a, AMP_PARAM_PRESENCE, 1.0f);
        Ampsim_reset(a);
        float r_loud = run_rms(a, 220.0f, 0.50f, 32768u, 16384u);
        Ampsim_reset(a);
        float r_soft = run_rms(a, 220.0f, 0.15f, 32768u, 16384u);
        float ratio = r_loud / (r_soft > 1e-6f ? r_soft : 1e-6f);
        printf("    compression ratio loud/soft = %.3f\n", ratio);
        CHECK(ratio > 0.95f && ratio < 2.9f, "cranked power stage compresses (ratio < linear 3.33)");

        Ampsim_set_param(a, AMP_PARAM_GAIN, 0.25f);
        Ampsim_set_param(a, AMP_PARAM_MASTER, 0.50f);
        Ampsim_set_param(a, AMP_PARAM_PRESENCE, 1.0f);
        Ampsim_reset(a);
        float r_soft_edge = run_rms(a, 220.0f, 0.15f, 32768u, 16384u);
        Ampsim_reset(a);
        float r_loud_edge = run_rms(a, 220.0f, 0.50f, 32768u, 16384u);
        float ratio_edge = r_loud_edge / (r_soft_edge > 1e-6f ? r_soft_edge : 1e-6f);
        printf("    edge-of-breakup ratio loud/soft = %.3f\n", ratio_edge);
        CHECK(ratio_edge > 1.30f && ratio_edge < 2.9f, "edge-of-breakup keeps pick dynamics");
    }

    /* 9. neve knob: bypass (0) vs full color (1) differ */
    {
        Ampsim_set_param(a, AMP_PARAM_GAIN, 0.4f);
        Ampsim_set_param(a, AMP_PARAM_BASS, 0.5f);
        Ampsim_set_param(a, AMP_PARAM_MID, 0.5f);
        Ampsim_set_param(a, AMP_PARAM_TREBLE, 0.5f);
        Ampsim_set_param(a, AMP_PARAM_MASTER, 0.5f);
        Ampsim_set_param(a, AMP_PARAM_LEVEL, 0.8f);
        Ampsim_set_param(a, AMP_PARAM_NEVE, 0.0f);
        Ampsim_reset(a);
        float r_neve_off = run_rms(a, 700.0f, 0.15f, 32768u, 16384u);
        Ampsim_set_param(a, AMP_PARAM_NEVE, 1.0f);
        Ampsim_reset(a);
        float r_neve_on = run_rms(a, 700.0f, 0.15f, 32768u, 16384u);
        CHECK(r_neve_on > 1.03f * r_neve_off, "neve knob changes coloration");
    }

    /* 10. cabtype: 1x12 vs 2x12 vs 4x12 differ (multi-speaker summing +
     *      per-cab voicing); the low end also shifts (2x12/4x12 bigger body). */
    {
        Ampsim_set_param(a, AMP_PARAM_NEVE, 1.0f);
        Ampsim_set_param(a, AMP_PARAM_PRESENCE, 1.0f);
        Ampsim_set_param(a, AMP_PARAM_CABTYPE, 0.0f);   /* 1x12 */
        Ampsim_reset(a);
        float r_cab_1x12 = run_rms(a, 6000.0f, 0.06f, 32768u, 16384u);
        Ampsim_set_param(a, AMP_PARAM_CABTYPE, 1.0f);   /* 2x12 */
        Ampsim_reset(a);
        float r_cab_2x12 = run_rms(a, 900.0f, 0.12f, 32768u, 16384u);
        Ampsim_set_param(a, AMP_PARAM_CABTYPE, 2.0f);   /* 4x12 */
        Ampsim_reset(a);
        float r_cab_4x12 = run_rms(a, 900.0f, 0.12f, 32768u, 16384u);
        CHECK(fabsf(r_cab_1x12 - r_cab_2x12) > 0.01f * r_cab_1x12, "cabtype 1x12 vs 2x12 differ");
        CHECK(fabsf(r_cab_4x12 - r_cab_2x12) > 0.01f * r_cab_2x12, "cabtype 2x12 vs 4x12 differ");
        Ampsim_set_param(a, AMP_PARAM_CABTYPE, 0.0f);
        Ampsim_reset(a);
        float r_low_1x12 = run_rms(a, 110.0f, 0.10f, 32768u, 16384u);
        Ampsim_set_param(a, AMP_PARAM_CABTYPE, 2.0f);   /* 4x12: bigger low end */
        Ampsim_reset(a);
        float r_low_4x12 = run_rms(a, 110.0f, 0.10f, 32768u, 16384u);
        CHECK(r_low_4x12 > r_low_1x12, "4x12 has more low end than 1x12");
        CHECK(Ampsim_get_param(a, AMP_PARAM_CABTYPE) == 2.0f, "cabtype param round-trip");
    }

    /* 11. presence knob: off (0) vs full (1) differ at 3.5 kHz */
    {
        Ampsim_set_param(a, AMP_PARAM_PRESENCE, 0.0f);
        Ampsim_reset(a);
        float r_pres_off = run_rms(a, 3500.0f, 0.06f, 32768u, 16384u);
        Ampsim_set_param(a, AMP_PARAM_PRESENCE, 1.0f);
        Ampsim_reset(a);
        float r_pres_on = run_rms(a, 3500.0f, 0.06f, 32768u, 16384u);
        CHECK(r_pres_on > 1.03f * r_pres_off, "presence knob boosts speaker resonance");
    }

    /* 12. input knob scales the input stage (and drives saturation) */
    {
        Ampsim_set_param(a, AMP_PARAM_GAIN, 0.4f);
        Ampsim_set_param(a, AMP_PARAM_BASS, 0.5f);
        Ampsim_set_param(a, AMP_PARAM_MID, 0.5f);
        Ampsim_set_param(a, AMP_PARAM_TREBLE, 0.5f);
        Ampsim_set_param(a, AMP_PARAM_MASTER, 0.5f);
        Ampsim_set_param(a, AMP_PARAM_LEVEL, 0.8f);
        Ampsim_set_param(a, AMP_PARAM_INPUT, 1.0f);
        Ampsim_reset(a);
        float r_in_hi = run_rms(a, 220.0f, 0.10f, 16384u, 8192u);
        Ampsim_set_param(a, AMP_PARAM_INPUT, 0.0f);
        Ampsim_reset(a);
        float r_in_lo = run_rms(a, 220.0f, 0.10f, 16384u, 8192u);
        /* 1.25x vs 0.125x input gain -> output should differ strongly */
        CHECK(r_in_hi > 4.0f * r_in_lo, "input knob scales the amp input");
        CHECK(fabsf(Ampsim_input_gain(1.0f) - 1.25f) < 1e-6f, "input gain ref = 1.25 at max");
        CHECK(fabsf(Ampsim_input_gain(0.0f) - 0.125f) < 1e-6f, "input gain floor = 0.125");
    }

    /* 13. cabtype switch changes the IR/gain character (round-trip) */
    {
        Ampsim_set_param(a, AMP_PARAM_GAIN, 0.30f);
        Ampsim_set_param(a, AMP_PARAM_BASS, 0.5f);
        Ampsim_set_param(a, AMP_PARAM_MID, 0.5f);
        Ampsim_set_param(a, AMP_PARAM_TREBLE, 0.5f);
        Ampsim_set_param(a, AMP_PARAM_MASTER, 0.5f);
        Ampsim_set_param(a, AMP_PARAM_LEVEL, 0.8f);
        Ampsim_set_param(a, AMP_PARAM_CABTYPE, 0.0f);
        Ampsim_reset(a);
        float r_c0 = run_rms(a, 900.0f, 0.30f, 32768u, 16384u);
        Ampsim_set_param(a, AMP_PARAM_CABTYPE, 1.0f);
        Ampsim_reset(a);
        float r_c1 = run_rms(a, 900.0f, 0.30f, 32768u, 16384u);
        CHECK(fabsf(r_c1 - r_c0) > 0.005f * r_c0, "cabtype changes the tone");
        CHECK(Ampsim_get_param(a, AMP_PARAM_CABTYPE) == 1.0f, "cabtype param round-trip");
    }

    /* 14. redundant same-value set_param must not reset filter states
     *      (regression: VOICE used to reload every biquad per block -> pops) */
    {
        uint32_t need2 = Ampsim_state_size();
        size_t nf2 = (need2 + sizeof(float) - 1u) / sizeof(float);
        float* memB = (float*)calloc(nf2, sizeof(float));
        CHECK(memB != NULL, "alloc control instance");
        if (memB) {
            Ampsim* b = Ampsim_init(memB, (uint32_t)(nf2 * sizeof(float)), 44100.0f);
            /* align every param on both instances */
            Ampsim_set_param(a, AMP_PARAM_INPUT, 1.0f);
            Ampsim_set_param(b, AMP_PARAM_INPUT, 1.0f);
            Ampsim_set_param(a, AMP_PARAM_GAIN, 0.5f);
            Ampsim_set_param(b, AMP_PARAM_GAIN, 0.5f);
            Ampsim_set_param(a, AMP_PARAM_BASS, 0.5f);
            Ampsim_set_param(b, AMP_PARAM_BASS, 0.5f);
            Ampsim_set_param(a, AMP_PARAM_MID, 0.5f);
            Ampsim_set_param(b, AMP_PARAM_MID, 0.5f);
            Ampsim_set_param(a, AMP_PARAM_TREBLE, 0.5f);
            Ampsim_set_param(b, AMP_PARAM_TREBLE, 0.5f);
            Ampsim_set_param(a, AMP_PARAM_MASTER, 0.5f);
            Ampsim_set_param(b, AMP_PARAM_MASTER, 0.5f);
            Ampsim_set_param(a, AMP_PARAM_LEVEL, 0.8f);
            Ampsim_set_param(b, AMP_PARAM_LEVEL, 0.8f);
            Ampsim_set_param(a, AMP_PARAM_NEVE, 1.0f);
            Ampsim_set_param(b, AMP_PARAM_NEVE, 1.0f);
            Ampsim_set_param(a, AMP_PARAM_PRESENCE, 1.0f);
            Ampsim_set_param(b, AMP_PARAM_PRESENCE, 1.0f);
            Ampsim_set_param(a, AMP_PARAM_CABTYPE, 0.0f);
            Ampsim_set_param(b, AMP_PARAM_CABTYPE, 0.0f);
            Ampsim_reset(a); Ampsim_reset(b);
            int mismatch = 0;
            for (int i = 0; i < 16384; ++i) {
                float t = (float)i / 44100.0f;
                float in = 0.4f * sinf(2.0f * 3.14159265f * 220.0f * t);
                if ((i % 64) == 0) Ampsim_set_param(a, AMP_PARAM_CABTYPE, 0.0f); /* redundant */
                float oa = 0.0f, ob = 0.0f;
                Ampsim_process(a, in, &oa);
                Ampsim_process(b, in, &ob);
                if (fabsf(oa - ob) > 1e-5f) { mismatch = 1; break; }
            }
            CHECK(!mismatch, "redundant cabtype set_param does not reset filters");
            free(memB);
        }
    }

    /* 15. init rejects too-small buffer */
    CHECK(Ampsim_init(mem, need - 1u, 44100.0f) == NULL, "rejects small buffer");

    /* 16. cabinet IR is a true convolution: one impulse in, energy must
     *      keep ringing out for hundreds of samples after the input stops,
     *      and decay toward zero by the end (kernel length 1024). */
    {
        Ampsim_set_param(a, AMP_PARAM_GAIN, 0.2f);   /* clean: no clip coloring */
        Ampsim_set_param(a, AMP_PARAM_BASS, 0.5f);
        Ampsim_set_param(a, AMP_PARAM_MID, 0.5f);
        Ampsim_set_param(a, AMP_PARAM_TREBLE, 0.5f);
        Ampsim_set_param(a, AMP_PARAM_MASTER, 0.4f);
        Ampsim_set_param(a, AMP_PARAM_LEVEL, 0.8f);
        Ampsim_reset(a);
        float ov = 0.0f;
        Ampsim_process(a, 1.0f, &ov);       /* single impulse */
        int tail_nz = 0, late_nz = 0;
        double e_tail = 0.0, e_late = 0.0;
        for (int i = 1; i < 1024; ++i) {
            Ampsim_process(a, 0.0f, &ov);
            if (i >= 64 && i < 800) { if (ov != 0.0f) tail_nz = 1; e_tail += (double)ov * ov; }
            if (i >= 900) { e_late += (double)ov * ov; if (ov != 0.0f) late_nz = 1; }
        }
        CHECK(tail_nz == 1, "IR tail rings after the impulse");
        CHECK(e_tail > 1e-8, "IR tail carries energy");
        CHECK(late_nz == 1 && e_late < e_tail, "IR tail decays toward zero");
    }

    /* 17. cabtype switch mid-stream must not pop: swapping the IR pointer
     *      with a shared delay buffer keeps the convolution continuous (no
     *      reset). */
    {
        Ampsim_set_param(a, AMP_PARAM_GAIN, 0.4f);
        Ampsim_set_param(a, AMP_PARAM_BASS, 0.5f);
        Ampsim_set_param(a, AMP_PARAM_MID, 0.5f);
        Ampsim_set_param(a, AMP_PARAM_TREBLE, 0.5f);
        Ampsim_set_param(a, AMP_PARAM_MASTER, 0.5f);
        Ampsim_set_param(a, AMP_PARAM_LEVEL, 0.8f);
        Ampsim_set_param(a, AMP_PARAM_CABTYPE, 0.0f);
        Ampsim_reset(a);
        float prev = 0.0f, worst = 0.0f;
        for (int i = 0; i < 8192; ++i) {
            float t = (float)i / 44100.0f;
            float in = 0.2f * sinf(2.0f * 3.14159265f * 220.0f * t);
            float ov2 = 0.0f;
            Ampsim_process(a, in, &ov2);
            if (i == 4096) Ampsim_set_param(a, AMP_PARAM_CABTYPE, 1.0f); /* mid-stream */
            float d = ov2 - prev;
            if (d < 0.0f) d = -d;
            if (d > worst) worst = d;
            prev = ov2;
        }
        /* a pop would show up as a step far larger than the sine's slope */
        float slope = 0.2f * 2.0f * 3.14159265f * 220.0f / 44100.0f;
        printf("    cabtype-switch worst step = %.5f (limit %.4f)\n", worst, 40.0f * slope + 1e-5f);
        CHECK(worst < 40.0f * slope + 1e-5f, "cabtype switch mid-stream does not pop");
    }

    /* 18. redundant same-value BASS/MID/TREBLE set_param must not reset the
     *      tone filters (regression: hosts re-send every param each audio
     *      block; tone_peaking used to zero v1/v2 -> block-rate clicks/buzz,
     *      worst at extreme tone settings). */
    {
        uint32_t need2 = Ampsim_state_size();
        size_t nf2 = (need2 + sizeof(float) - 1u) / sizeof(float);
        float* memB = (float*)calloc(nf2, sizeof(float));
        CHECK(memB != NULL, "alloc tone-regression instance");
        if (memB) {
            Ampsim* b = Ampsim_init(memB, (uint32_t)(nf2 * sizeof(float)), 44100.0f);
            float tv[3][2] = { {1.0f, 0.0f}, {1.0f, 1.0f}, {1.0f, 0.0f} }; /* extreme */
            for (int k = 0; k < 3; ++k) {
                Ampsim_set_param(a, (AmpsimParam)(AMP_PARAM_BASS + k), tv[k][0]);
                Ampsim_set_param(b, (AmpsimParam)(AMP_PARAM_BASS + k), tv[k][0]);
            }
            Ampsim_set_param(a, AMP_PARAM_GAIN, 1.0f); Ampsim_set_param(b, AMP_PARAM_GAIN, 1.0f);
            Ampsim_set_param(a, AMP_PARAM_MASTER, 1.0f); Ampsim_set_param(b, AMP_PARAM_MASTER, 1.0f);
            Ampsim_set_param(a, AMP_PARAM_LEVEL, 1.0f); Ampsim_set_param(b, AMP_PARAM_LEVEL, 1.0f);
            Ampsim_set_param(a, AMP_PARAM_INPUT, 1.0f); Ampsim_set_param(b, AMP_PARAM_INPUT, 1.0f);
            Ampsim_set_param(a, AMP_PARAM_NEVE, 1.0f); Ampsim_set_param(b, AMP_PARAM_NEVE, 1.0f);
            Ampsim_set_param(a, AMP_PARAM_PRESENCE, 1.0f); Ampsim_set_param(b, AMP_PARAM_PRESENCE, 1.0f);
            Ampsim_set_param(a, AMP_PARAM_CABTYPE, 0.0f); Ampsim_set_param(b, AMP_PARAM_CABTYPE, 0.0f);
            Ampsim_reset(a); Ampsim_reset(b);
            int mismatch = 0;
            for (int i = 0; i < 16384; ++i) {
                float t = (float)i / 44100.0f;
                float in = 0.25f * sinf(2.0f * 3.14159265f * 220.0f * t);
                if ((i % 64) == 0) {
                    for (int k = 0; k < 3; ++k) {
                        Ampsim_set_param(a, (AmpsimParam)(AMP_PARAM_BASS + k), tv[k][0]);
                        Ampsim_set_param(b, (AmpsimParam)(AMP_PARAM_BASS + k), tv[k][0]);
                    }
                }
                float oa = 0.0f, ob = 0.0f;
                Ampsim_process(a, in, &oa);
                Ampsim_process(b, in, &ob);
                if (fabsf(oa - ob) > 1e-5f) { mismatch = 1; break; }
            }
            CHECK(!mismatch, "redundant tone set_param does not reset filters");
            free(memB);
        }
    }

    /* 19. max-gain distortion must survive the cab: at gain=1/master=1 the
     *      220 Hz 3rd harmonic must be strong at the output. The cabinet IR
     *      used to have deep mid nulls that ate the drive harmonics; with
     *      the harder drive clip and flattened IR the 3rd harmonic reads
     *      above -16 dB (was ~-20.5 dB). */
    {
        Ampsim_set_param(a, AMP_PARAM_GAIN, 1.0f);
        Ampsim_set_param(a, AMP_PARAM_BASS, 0.5f);
        Ampsim_set_param(a, AMP_PARAM_MID, 0.5f);
        Ampsim_set_param(a, AMP_PARAM_TREBLE, 0.5f);
        Ampsim_set_param(a, AMP_PARAM_MASTER, 1.0f);
        Ampsim_set_param(a, AMP_PARAM_LEVEL, 1.0f);
        Ampsim_reset(a);
        int warm = (int)(0.5f * 44100.0f);
        int n = 8192;
        float* s = (float*)malloc(n * sizeof(float));
        CHECK(s != NULL, "alloc distortion probe");
        if (s) {
            float o = 0.0f;
            for (int i = 0; i < warm + n; ++i) {
                float t = (float)i / 44100.0f;
                Ampsim_process(a, 0.25f * sinf(2.0f * 3.14159265f * 220.0f * t), &o);
                if (i >= warm) s[i - warm] = o;
            }
            float h3 = 0.0f;
            {
                double re = 0.0, im = 0.0;
                double w = 2.0 * 3.14159265358979 * 3.0 * 220.0 / 44100.0;
                for (int i = 0; i < n; ++i) { re += s[i] * cos(w * i); im += s[i] * sin(w * i); }
                h3 = (float)(2.0 * sqrt(re * re + im * im) / n);
            }
            printf("    max-gain 3rd harmonic = %.1f dB\n", 20.0f * log10f(h3 + 1e-9f));
            CHECK(h3 > 0.12f, "max-gain distortion reaches the output (3rd harmonic > -18 dB)");
            free(s);
        }
    }

    /* 20. max gain must distort even a quiet input. Regression: with the
     *      drive capped at ~13x a -30 dBFS DI never reached the clip knee,
     *      so "gain maxed" still sounded clean. The drive range now spans
     *      ~60x so even -30 dBFS breaks up at gain=1. */
    {
        Ampsim_set_param(a, AMP_PARAM_GAIN, 1.0f);
        Ampsim_set_param(a, AMP_PARAM_MASTER, 1.0f);
        Ampsim_set_param(a, AMP_PARAM_LEVEL, 1.0f);
        Ampsim_reset(a);
        int warm = (int)(0.5f * 44100.0f);
        int n = 8192;
        float* s = (float*)malloc(n * sizeof(float));
        CHECK(s != NULL, "alloc quiet-distortion probe");
        if (s) {
            float o = 0.0f;
            for (int i = 0; i < warm + n; ++i) {
                float t = (float)i / 44100.0f;
                Ampsim_process(a, 0.03f * sinf(2.0f * 3.14159265f * 220.0f * t), &o);
                if (i >= warm) s[i - warm] = o;
            }
            double re = 0.0, im = 0.0;
            double w = 2.0 * 3.14159265358979 * 3.0 * 220.0 / 44100.0;
            for (int i = 0; i < n; ++i) { re += s[i] * cos(w * i); im += s[i] * sin(w * i); }
            float h3 = (float)(2.0 * sqrt(re * re + im * im) / n);
            printf("    max-gain 3rd harmonic @ -30dBFS = %.1f dB\n",
                   20.0f * log10f(h3 + 1e-9f));
            CHECK(h3 > 0.08f, "max gain distorts even a -30 dBFS input (h3 > -22 dB)");
            free(s);
        }
    }

    free(mem);
    if (failures == 0) { printf("\nALL TESTS PASSED\n"); return 0; }
    printf("\n%d FAILURES\n", failures);
    return 1;
}

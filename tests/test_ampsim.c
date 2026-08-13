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

    /* 8. dynamic compression: loud input compresses vs quiet input */
    {
        Ampsim_set_param(a, AMP_PARAM_GAIN, 0.8f);
        Ampsim_set_param(a, AMP_PARAM_BASS, 0.5f);
        Ampsim_set_param(a, AMP_PARAM_MID, 0.5f);
        Ampsim_set_param(a, AMP_PARAM_TREBLE, 0.5f);
        Ampsim_set_param(a, AMP_PARAM_MASTER, 0.8f);
        Ampsim_set_param(a, AMP_PARAM_LEVEL, 0.8f);
        Ampsim_reset(a);
        float r_loud = run_rms(a, 220.0f, 0.50f, 32768u, 16384u);
        Ampsim_reset(a);
        float r_soft = run_rms(a, 220.0f, 0.15f, 32768u, 16384u);
        float ratio = r_loud / (r_soft > 1e-6f ? r_soft : 1e-6f);
        printf("    compression ratio loud/soft = %.3f\n", ratio);
        CHECK(ratio > 1.2f && ratio < 2.9f, "loud input compresses (ratio < linear 3.33)");
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

    /* 10. cab knob: dark (0) vs bright (1) differ at 6 kHz */
    {
        Ampsim_set_param(a, AMP_PARAM_NEVE, 1.0f);
        Ampsim_set_param(a, AMP_PARAM_PRESENCE, 1.0f);
        Ampsim_set_param(a, AMP_PARAM_CAB, 0.0f);
        Ampsim_reset(a);
        float r_cab_dark = run_rms(a, 6000.0f, 0.06f, 32768u, 16384u);
        Ampsim_set_param(a, AMP_PARAM_CAB, 1.0f);
        Ampsim_reset(a);
        float r_cab_bright = run_rms(a, 6000.0f, 0.06f, 32768u, 16384u);
        CHECK(r_cab_bright > 1.03f * r_cab_dark, "cab knob shifts high-end voicing");
    }

    /* 11. presence knob: off (0) vs full (1) differ at 3.5 kHz */
    {
        Ampsim_set_param(a, AMP_PARAM_CAB, 0.5f);
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

    /* 13. voice switch: Emo/Edge raises the gain base (earlier breakup) */
    {
        Ampsim_set_param(a, AMP_PARAM_GAIN, 0.30f);
        Ampsim_set_param(a, AMP_PARAM_BASS, 0.5f);
        Ampsim_set_param(a, AMP_PARAM_MID, 0.5f);
        Ampsim_set_param(a, AMP_PARAM_TREBLE, 0.5f);
        Ampsim_set_param(a, AMP_PARAM_MASTER, 0.5f);
        Ampsim_set_param(a, AMP_PARAM_LEVEL, 0.8f);
        Ampsim_set_param(a, AMP_PARAM_VOICE, 0.0f);
        Ampsim_reset(a);
        float r_nash = run_rms(a, 220.0f, 0.25f, 32768u, 16384u);
        Ampsim_set_param(a, AMP_PARAM_VOICE, 1.0f);
        Ampsim_reset(a);
        float r_emo = run_rms(a, 220.0f, 0.25f, 32768u, 16384u);
        CHECK(r_emo > 1.02f * r_nash, "Emo/Edge voice breaks up earlier (higher gain base)");
        CHECK(Ampsim_get_param(a, AMP_PARAM_VOICE) == 1.0f, "voice param round-trip");
    }

    /* 14. init rejects too-small buffer */
    CHECK(Ampsim_init(mem, need - 1u, 44100.0f) == NULL, "rejects small buffer");

    free(mem);
    if (failures == 0) { printf("\nALL TESTS PASSED\n"); return 0; }
    printf("\n%d FAILURES\n", failures);
    return 1;
}
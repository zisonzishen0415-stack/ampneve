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

int main(void) {
    uint32_t need = Ampsim_state_size();
    printf("state_size = %u bytes\n", (unsigned)need);
    CHECK(need > 0 && need < 65536u, "state size sane");

    /* alignment-safe scratch (float buffer) */
    size_t nf = (need + sizeof(float) - 1u) / sizeof(float);
    float* mem = (float*)calloc(nf, sizeof(float));
    CHECK(mem != NULL, "alloc scratch");
    if (!mem) return 1;

    Ampsim* a = Ampsim_init(mem, (uint32_t)(nf * sizeof(float)), 44100.0f);
    CHECK(a != NULL, "init ok");

    /* default params in range */
    CHECK(Ampsim_get_param(a, AMP_PARAM_DRIVE) >= 0.0f && Ampsim_get_param(a, AMP_PARAM_DRIVE) <= 1.0f, "drive default in range");

    /* 1. unity passthrough: drive=0 neve=0 cab=0 level=0.5 -> gain 1.0 */
    Ampsim_set_param(a, AMP_PARAM_DRIVE, 0.0f);
    Ampsim_set_param(a, AMP_PARAM_NEVE, 0.0f);
    Ampsim_set_param(a, AMP_PARAM_CAB, 0.0f);
    Ampsim_set_param(a, AMP_PARAM_BASS, 0.0f);
    Ampsim_set_param(a, AMP_PARAM_TONE, 0.5f);
    Ampsim_set_param(a, AMP_PARAM_LEVEL, 0.5f);
    Ampsim_reset(a);
    float o = 0.0f;
    Ampsim_process(a, 0.3f, &o);
    CHECK(fabsf(o - 0.3f) < 1e-4f, "unity passthrough (level=0.5, all off)");

    /* 2. soft clip bounds: big input with full drive stays bounded */
    Ampsim_set_param(a, AMP_PARAM_DRIVE, 1.0f);
    Ampsim_set_param(a, AMP_PARAM_LEVEL, 0.0f);   /* gain 0.5 */
    Ampsim_reset(a);
    float peak = 0.0f;
    for (int i = 0; i < 4096; ++i) {
        Ampsim_process(a, 8.0f, &o);
        if (fabsf(o) > peak) peak = fabsf(o);
    }
    CHECK(peak > 0.0f && peak < 1.0f, "soft clip bounded");

    /* 3. no NaN/Inf over a long run with a sine */
    Ampsim_set_param(a, AMP_PARAM_DRIVE, 0.7f);
    Ampsim_set_param(a, AMP_PARAM_NEVE, 0.8f);
    Ampsim_set_param(a, AMP_PARAM_CAB, 1.0f);
    Ampsim_set_param(a, AMP_PARAM_TONE, 0.4f);
    Ampsim_set_param(a, AMP_PARAM_BASS, 0.6f);
    Ampsim_set_param(a, AMP_PARAM_LEVEL, 1.0f);
    Ampsim_reset(a);
    int bad = 0;
    double sum2 = 0.0;
    unsigned cnt = 0;
    for (unsigned i = 0; i < 200000u; ++i) {
        float t = (float)i / 44100.0f;
        float in = 0.5f * sinf(2.0f * 3.14159265f * 220.0f * t) + 0.25f * sinf(2.0f * 3.14159265f * 660.0f * t);
        Ampsim_process(a, in, &o);
        if (!(o == o) || o > 1e6f || o < -1e6f) { bad = 1; break; }
        sum2 += (double)o * o;
        ++cnt;
    }
    CHECK(!bad, "stable, no NaN/Inf on sine run");
    CHECK(cnt > 0u && sum2 > 0.0, "output has energy");

    /* 4. param clamping */
    Ampsim_set_param(a, AMP_PARAM_DRIVE, 5.0f);
    CHECK(Ampsim_get_param(a, AMP_PARAM_DRIVE) == 1.0f, "clamps high");
    Ampsim_set_param(a, AMP_PARAM_DRIVE, -1.0f);
    CHECK(Ampsim_get_param(a, AMP_PARAM_DRIVE) == 0.0f, "clamps low");

    /* 5. reset clears filter state (output after reset from steady state) */
    Ampsim_set_param(a, AMP_PARAM_DRIVE, 0.0f);
    Ampsim_set_param(a, AMP_PARAM_NEVE, 1.0f);
    Ampsim_set_param(a, AMP_PARAM_CAB, 1.0f);
    Ampsim_set_param(a, AMP_PARAM_LEVEL, 0.5f);
    Ampsim_reset(a);
    Ampsim_process(a, 0.1f, &o);
    float after1 = o;
    Ampsim_reset(a);
    Ampsim_process(a, 0.1f, &o);
    CHECK(fabsf(o - after1) < 1e-5f, "reset deterministic");

    /* 6. init rejects too-small buffer */
    CHECK(Ampsim_init(mem, need - 1u, 44100.0f) == NULL, "rejects small buffer");

    free(mem);
    if (failures == 0) { printf("\nALL TESTS PASSED\n"); return 0; }
    printf("\n%d FAILURES\n", failures);
    return 1;
}

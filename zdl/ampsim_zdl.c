/*
 * ampsim_zdl.c -- AmpNeve for the Zoom MultiStomp (ZDL).
 *
 * Wraps the core (core/ampsim.c, inlined below) in the Zoom runtime ABI:
 *   - persistent state lives in the host-managed ctx[3] arena (2 x 144
 *     floats, ~1.2 KB total - the arena is hundreds of KB)
 *   - params come from the params[] table (see ampsim_zdl_params.h)
 *   - block processing: 8 samples L + 8 samples R, channel-interleaved
 *   - ctx[11]/ctx[12] magic shuttle preserved
 *
 * Six knobs across two LineSel pages (mirrors the VST):
 *   P1: Gain / Bass / Mid,  P2: Treble / Master / Level.
 * Cab voicing and Neve coloration are fixed internally (brand sound).
 *
 * Build (needs TI C6000 CGT, see zdl/README.md):
 *   cl6x --c99 --opt_level=2 --opt_for_space=3 -mv6740 --abi=eabi \
 *        --mem_model:data=far --include_path=<repo>/core \
 *        -c ampsim_zdl.c -o ampsim_zdl.obj
 *   python3 build.py
 */
#include <stdint.h>

/* inline the ZDL-safe core (single-obj build: the Zoom linker takes one .obj) */
#include "../core/ampsim.c"

#include "ampsim_zdl_params.h"

#ifndef AMP_DRV_AUDIO_FUNC
#define AMP_DRV_AUDIO_FUNC Fx_AMP_AmpNeve
#endif

#define AMP_DO_PRAGMA(x) _Pragma(#x)
#define AMP_EXPAND_PRAGMA(x) AMP_DO_PRAGMA(x)
#define AMP_CODE_SECTION(func) AMP_EXPAND_PRAGMA(CODE_SECTION(func, ".audio"))
#define AMP_ALWAYS_INLINE(func) AMP_EXPAND_PRAGMA(FUNC_ALWAYS_INLINE(func))
AMP_CODE_SECTION(AMP_DRV_AUDIO_FUNC)

#define ZDL_PTR(type, word) ((type)(uintptr_t)(word))

#define AMP_MAGIC   0x414D504Eu   /* 'AMPN' */
#define AMP_VERSION 1u
#define AMP_RAW_MAX 0.14f
#define AMP_RAW_TO_NORM 7.1428571f

/* one Ampsim instance per channel; state float count is fixed generously
   (Ampsim_state_size() is 544 bytes < 144 floats = 576 bytes). */
#define AMP_STATE_FLOATS 144u

typedef struct AmpZdlState {
    uint32_t magic;
    uint32_t version;
    uint32_t initialized;
    uint32_t reserved;
    float memL[AMP_STATE_FLOATS];
    float memR[AMP_STATE_FLOATS];
} AmpZdlState;

AMP_ALWAYS_INLINE(amp_align4)
static inline uintptr_t amp_align4(uintptr_t x)
{
    return (x + 3u) & ~(uintptr_t)3u;
}

AMP_ALWAYS_INLINE(amp_clamp01)
static inline float amp_clamp01(float x)
{
    if (x < 0.0f) return 0.0f;
    if (x > 1.0f) return 1.0f;
    return x;
}

/* Normalize a host param value (0..100 raw, ~0.14 scaled, or 0..1) to 0..1,
 * mirroring the OTT param_norm pattern. */
AMP_ALWAYS_INLINE(amp_param_norm)
static inline float amp_param_norm(float raw, float fallback_norm)
{
    if (raw != raw) return amp_clamp01(fallback_norm);          /* NaN */
    if (raw < 0.0f) return amp_clamp01(fallback_norm);
    if (raw <= 0.0001f) return 0.0f;
    if (raw <= (AMP_RAW_MAX * 1.1f)) return amp_clamp01(raw * AMP_RAW_TO_NORM);
    if (raw <= 1.0f) return amp_clamp01(raw);
    if (raw <= 100.0f) return amp_clamp01(raw * 0.01f);
    return amp_clamp01(fallback_norm);
}

AMP_ALWAYS_INLINE(amp_zdl_init)
static inline void amp_zdl_init(AmpZdlState *st)
{
    unsigned i;
    for (i = 0; i < AMP_STATE_FLOATS; ++i) { st->memL[i] = 0.0f; st->memR[i] = 0.0f; }
    if (Ampsim_init(&st->memL, AMP_STATE_FLOATS * sizeof(float), 44100.0f) == 0) return;
    if (Ampsim_init(&st->memR, AMP_STATE_FLOATS * sizeof(float), 44100.0f) == 0) return;
    st->magic = AMP_MAGIC;
    st->version = AMP_VERSION;
    st->initialized = 1u;
    st->reserved = 0u;
}

void AMP_DRV_AUDIO_FUNC(unsigned int *ctx)
{
    float *params = ZDL_PTR(float *, ctx[1]);
    float *fxBuf  = ZDL_PTR(float *, ctx[5]);

    unsigned int *magicSrc = ZDL_PTR(unsigned int *, ctx[12]);
    unsigned int *magicDst = ZDL_PTR(unsigned int *, *(unsigned int *)ZDL_PTR(unsigned int *, ctx[11]));
    *magicDst = *magicSrc;

    if (params[0] < 0.5f) return;   /* effect off: leave Fx buffer for the dry mix */

    volatile unsigned int *desc = ZDL_PTR(volatile unsigned int *, ctx[3]);
    if (!desc) return;

    uintptr_t base = (uintptr_t)desc[0];
    uintptr_t end  = (uintptr_t)desc[1];
    unsigned int span = desc[2];
    uintptr_t stateBase = amp_align4(base);
    uintptr_t requiredEnd = stateBase + sizeof(AmpZdlState);
    uintptr_t bytes = end - base;

    if (base == 0u || end <= base) return;
    if ((base & 3u) != 0u || (end & 3u) != 0u || (span & 3u) != 0u) return;
    if (bytes < sizeof(AmpZdlState) || span < bytes) return;
    if (requiredEnd > end) return;

    AmpZdlState *st = (AmpZdlState *)stateBase;
    if (st->magic != AMP_MAGIC || st->version != AMP_VERSION || !st->initialized) {
        amp_zdl_init(st);
        return;
    }

    float gain   = amp_param_norm(params[AMPNEVE_GAIN_SLOT],   AMPNEVE_GAIN_DEFAULT_NORM);
    float bass   = amp_param_norm(params[AMPNEVE_BASS_SLOT],   AMPNEVE_BASS_DEFAULT_NORM);
    float mid    = amp_param_norm(params[AMPNEVE_MID_SLOT],    AMPNEVE_MID_DEFAULT_NORM);
    float treble = amp_param_norm(params[AMPNEVE_TREBLE_SLOT], AMPNEVE_TREBLE_DEFAULT_NORM);
    float master = amp_param_norm(params[AMPNEVE_MASTER_SLOT], AMPNEVE_MASTER_DEFAULT_NORM);
    float level  = amp_param_norm(params[AMPNEVE_LEVEL_SLOT],  AMPNEVE_LEVEL_DEFAULT_NORM);

    Ampsim *aL = (Ampsim *)&st->memL;
    Ampsim *aR = (Ampsim *)&st->memR;
    Ampsim_set_param(aL, AMP_PARAM_GAIN,   gain);
    Ampsim_set_param(aL, AMP_PARAM_BASS,   bass);
    Ampsim_set_param(aL, AMP_PARAM_MID,    mid);
    Ampsim_set_param(aL, AMP_PARAM_TREBLE, treble);
    Ampsim_set_param(aL, AMP_PARAM_MASTER, master);
    Ampsim_set_param(aL, AMP_PARAM_LEVEL,  level);
    Ampsim_set_param(aR, AMP_PARAM_GAIN,   gain);
    Ampsim_set_param(aR, AMP_PARAM_BASS,   bass);
    Ampsim_set_param(aR, AMP_PARAM_MID,    mid);
    Ampsim_set_param(aR, AMP_PARAM_TREBLE, treble);
    Ampsim_set_param(aR, AMP_PARAM_MASTER, master);
    Ampsim_set_param(aR, AMP_PARAM_LEVEL,  level);

    int i;
    for (i = 0; i < 8; i++) {
        float oL, oR;
        Ampsim_process(aL, fxBuf[i],     &oL);
        Ampsim_process(aR, fxBuf[i + 8], &oR);
        fxBuf[i]     = oL;
        fxBuf[i + 8] = oR;
    }
}

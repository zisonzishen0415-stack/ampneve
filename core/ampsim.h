/* core/ampsim.h - AmpNeve: amp + cabinet + Neve-style coloration.
 * A from-scratch, ZDL-safe guitar amp/cabinet/preamp simulator.
 * Signal chain: soft clip (drive) -> Neve coloration (transformer even
 * harmonics + 1073-style EQ) -> cabinet voicing (HP/body/presence/LP,
 * dark..bright tone) + bass body -> level.
 *
 * ZDL-safe: no heap (caller memory), no double, no sinf/cosf/powf/logf,
 * no division, no large writable statics. All filter coefficients are
 * precomputed constants (tools/gen_coeffs.py), so the audio path is pure
 * multiply-add.
 */
#ifndef AMPNEVE_H
#define AMPNEVE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AMPNEVE_NUM_PARAMS 6u

typedef enum {
    AMP_PARAM_DRIVE = 0,   /* soft-clip drive amount, 0..1 */
    AMP_PARAM_TONE,        /* cabinet dark(0)..bright(1) */
    AMP_PARAM_LEVEL,       /* output level, 0..1.5 gain */
    AMP_PARAM_BASS,        /* low-mid body, 0..1 */
    AMP_PARAM_NEVE,        /* Neve coloration amount, 0..1 */
    AMP_PARAM_CAB          /* cabinet amount (dry/wet), 0..1 */
} AmpsimParam;

typedef struct Ampsim Ampsim;   /* opaque; state lives in caller memory */

/* Bytes of state memory needed (constant per build). */
uint32_t Ampsim_state_size(void);

/* Init a core in caller-provided memory (must be >= state_size, 4-byte
 * aligned). Returns NULL on bad args. */
Ampsim* Ampsim_init(void* mem, uint32_t bytes, float sample_rate);
void Ampsim_reset(Ampsim* a);

void Ampsim_set_param(Ampsim* a, AmpsimParam p, float v);
float Ampsim_get_param(const Ampsim* a, AmpsimParam p);

/* Mono processing (like the pedal). Stereo hosts should sum L+R to mono
 * (see the VST shell) or run two instances for true stereo. */
void Ampsim_process(Ampsim* a, float in, float* out);

#ifdef __cplusplus
}
#endif

#endif

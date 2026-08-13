/* core/ampsim.h - AmpNeve: boutique amp + cabinet + Neve-style coloration.
 * A from-scratch, ZDL-safe guitar amp simulator.
 *
 * Signal chain (v1 boutique):
 *   input stage (light asymmetric saturation)
 *   -> gain stage with touch dynamics (envelope-modulated drive)
 *   -> tone network (bass/mid/treble, interacting)
 *   -> power stage (softer clip + sag compression)
 *   -> output transformer (Neve even harmonics + 1073 EQ, brand color)
 *   -> speaker resonance + cabinet voicing (dark..bright)
 *   -> cabinet IR convolution (1024-tap miked-cab kernel: mic
 *      pickup + speaker-cone resonances + room, static per voice)
 *   -> level
 *
 * Voice switch (0 = Nashville session, 1 = Emo/Edge): swaps the Neve EQ,
 * cab voicing and the cabinet IR, and raises the gain-stage base so edge
 * breakup arrives earlier - one pedal-style toggle, per-voice static IRs.
 *
 * ZDL-safe: no heap (caller memory), no double, no sinf/cosf/powf/logf,
 * no division in the audio path, no large writable statics (the cabinet
 * IR is a static const kernel; only its rolling delay buffer is state).
 * Filter
 * coefficients are precomputed constants or updated at set_param time with
 * multiply-add + a fixed-point reciprocal approximation (no math library).
 */
#ifndef AMPNEVE_H
#define AMPNEVE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AMPNEVE_NUM_PARAMS 11u

typedef enum {
    AMP_PARAM_INPUT = 0,  /* input trim, 0..1 (1.0 = calibrated ref) */
    AMP_PARAM_GAIN,    /* preamp gain + touch-dynamics base, 0..1 */
    AMP_PARAM_BASS,        /* tone network low, 0..1 (0.5 = flat) */
    AMP_PARAM_MID,         /* tone network mid, 0..1 (0.5 = flat) */
    AMP_PARAM_TREBLE,      /* tone network high, 0..1 (0.5 = flat) */
    AMP_PARAM_MASTER,      /* power-stage drive + sag amount, 0..1 */
    AMP_PARAM_LEVEL,       /* output level, 0..1 (0.5..1.5 gain) */
    AMP_PARAM_NEVE,        /* Neve coloration amount, 0..1 (0 = bypass) */
    AMP_PARAM_CAB,         /* cabinet voicing, 0..1 (0 = dark, 1 = bright) */
    AMP_PARAM_PRESENCE,    /* speaker 3.5kHz resonance amount, 0..1 */
    AMP_PARAM_VOICE        /* voice: 0 = Nashville, 1 = Emo/Edge */
} AmpsimParam;

/* Maps the 0..1 Input knob to a linear input gain: 0.125x..1.25x,
 * where 1.0 = the calibrated reference (the historical fixed 1.25x). */
float Ampsim_input_gain(float v);

typedef struct Ampsim Ampsim;   /* opaque; state lives in caller memory */

uint32_t Ampsim_state_size(void);

Ampsim* Ampsim_init(void* mem, uint32_t bytes, float sample_rate);
void Ampsim_reset(Ampsim* a);

void Ampsim_set_param(Ampsim* a, AmpsimParam p, float v);
float Ampsim_get_param(const Ampsim* a, AmpsimParam p);

/* Mono processing (like the pedal). Stereo hosts sum L+R to mono or run
 * two instances for true stereo. */
void Ampsim_process(Ampsim* a, float in, float* out);

#ifdef __cplusplus
}
#endif

#endif

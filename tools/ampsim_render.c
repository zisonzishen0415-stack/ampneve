/* tools/ampsim_render.c - offline AmpNeve renderer.
 * Reads a 16-bit PCM WAV (mono or stereo), processes it through the core,
 * writes a stereo 16-bit WAV. Same code path as the VST/ZDL (no python).
 *
 * usage: ampsim_render <in.wav> <out.wav> [gain] [bass] [mid] [treble]
 *                                [master] [level] [neve] [cab] [presence]
 */
#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif
#include "ampsim.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define MAX_SAMPLES (16u * 1024u * 1024u)

static int read_wav(const char* path, float** out, unsigned* rate_out, unsigned* n_out) {
    FILE* f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "cannot open %s\n", path); return -1; }
    char hdr[12];
    if (fread(hdr, 1, 12, f) != 12 || memcmp(hdr, "RIFF", 4) != 0 || memcmp(hdr + 8, "WAVE", 4) != 0) {
        fprintf(stderr, "%s: not RIFF/WAVE\n", path); fclose(f); return -1;
    }
    unsigned rate = 0, ch = 0, bits = 0, data_len = 0;
    int found = 0;
    for (;;) {
        char ck[8];
        if (fread(ck, 1, 8, f) != 8) break;
        unsigned sz = (unsigned char)ck[4] | ((unsigned char)ck[5] << 8) | ((unsigned char)ck[6] << 16) | ((unsigned char)ck[7] << 24);
        if (memcmp(ck, "fmt ", 4) == 0) {
            unsigned char fmt[16];
            unsigned got = (unsigned)fread(fmt, 1, sz < 16 ? sz : 16, f);
            if (sz < 16 || got < 16) break;
            unsigned tag = fmt[0] | (fmt[1] << 8);
            ch = fmt[2] | (fmt[3] << 8);
            rate = (unsigned)fmt[4] | ((unsigned)fmt[5] << 8) | ((unsigned)fmt[6] << 16) | ((unsigned)fmt[7] << 24);
            bits = fmt[14] | (fmt[15] << 8);
            if (tag != 1 || bits != 16) { fprintf(stderr, "%s: only PCM16 (tag=%u bits=%u)\n", path, tag, bits); fclose(f); return -1; }
            if (sz > 16 && fseek(f, (long)(sz - 16), SEEK_CUR) != 0) break;
        } else if (memcmp(ck, "data", 4) == 0) {
            data_len = sz; found = 1; break;
        } else {
            if (fseek(f, (long)sz, SEEK_CUR) != 0) break;
        }
    }
    if (!found || ch == 0 || ch > 2 || rate == 0) { fprintf(stderr, "%s: bad WAV\n", path); fclose(f); return -1; }
    unsigned n = data_len / (ch * 2u);
    if (n == 0 || n > MAX_SAMPLES) { fprintf(stderr, "%s: too many frames (%u)\n", path, n); fclose(f); return -1; }
    float* mono = (float*)malloc(n * sizeof(float));
    if (!mono) { fclose(f); return -1; }
    for (unsigned i = 0; i < n; ++i) {
        short l, r;
        if (fread(&l, 2, 1, f) != 1) { n = i; break; }
        if (ch == 2) {
            if (fread(&r, 2, 1, f) != 1) { n = i; break; }
            mono[i] = ((float)l + (float)r) * 0.5f / 32768.0f;
        } else {
            mono[i] = (float)l / 32768.0f;
        }
    }
    fclose(f);
    *out = mono; *rate_out = rate; *n_out = n;
    return 0;
}

static int write_wav(const char* path, const float* l, const float* r, unsigned n, unsigned rate) {
    FILE* f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "cannot write %s\n", path); return -1; }
    unsigned data_bytes = n * 4u;
    unsigned riff = 36u + data_bytes;
    fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVE", 1, 4, f);
    unsigned fmt_sz = 16, tag = 1, ch = 2, bits = 16;
    unsigned block = ch * (bits / 8u), bps = rate * block;
    fwrite("fmt ", 1, 4, f); fwrite(&fmt_sz, 4, 1, f);
    fwrite(&tag, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&rate, 4, 1, f); fwrite(&bps, 4, 1, f);
    fwrite(&block, 2, 1, f); fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&data_bytes, 4, 1, f);
    for (unsigned i = 0; i < n; ++i) {
        short sl = (short)(l[i] * 32767.0f);
        short sr = (short)(r[i] * 32767.0f);
        fwrite(&sl, 2, 1, f); fwrite(&sr, 2, 1, f);
    }
    fclose(f);
    return 0;
}

static float peak_of(const float* x, unsigned n) {
    float p = 0.0f;
    for (unsigned i = 0; i < n; ++i) { float v = x[i] < 0 ? -x[i] : x[i]; if (v > p) p = v; }
    return p;
}

static float rms_of(const float* x, unsigned n) {
    float sum2 = 0.0f;
    for (unsigned i = 0; i < n; ++i) sum2 += x[i] * x[i];
    return n > 0u ? sqrtf(sum2 / (float)n) : 0.0f;
}

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: ampsim_render <in.wav> <out.wav> [gain] [bass] [mid] [treble] [master] [level] [neve] [cab] [presence]\n");
        return 1;
    }
    float gain   = argc > 3 ? (float)atof(argv[3]) : 0.45f;
    float bass   = argc > 4 ? (float)atof(argv[4]) : 0.50f;
    float mid    = argc > 5 ? (float)atof(argv[5]) : 0.50f;
    float treble = argc > 6 ? (float)atof(argv[6]) : 0.50f;
    float master   = argc > 7 ? (float)atof(argv[7]) : 0.50f;
    float level    = argc > 8 ? (float)atof(argv[8]) : 0.80f;
    float neve     = argc > 9 ? (float)atof(argv[9]) : 1.00f;
    float cab      = argc > 10 ? (float)atof(argv[10]) : 0.50f;
    float presence = argc > 11 ? (float)atof(argv[11]) : 1.00f;

    float* in = NULL; unsigned rate = 0, n = 0;
    if (read_wav(argv[1], &in, &rate, &n) != 0) return 1;

    uint32_t need = Ampsim_state_size();
    size_t nf = (need + sizeof(float) - 1u) / sizeof(float);
    float* mem = (float*)malloc(nf * sizeof(float));
    if (!mem) { fprintf(stderr, "alloc failed\n"); free(in); return 1; }
    Ampsim* a = Ampsim_init(mem, (uint32_t)(nf * sizeof(float)), (float)rate);
    if (!a) { fprintf(stderr, "init failed\n"); free(mem); free(in); return 1; }
    Ampsim_set_param(a, AMP_PARAM_GAIN, gain);
    Ampsim_set_param(a, AMP_PARAM_BASS, bass);
    Ampsim_set_param(a, AMP_PARAM_MID, mid);
    Ampsim_set_param(a, AMP_PARAM_TREBLE, treble);
    Ampsim_set_param(a, AMP_PARAM_MASTER, master);
    Ampsim_set_param(a, AMP_PARAM_LEVEL, level);
    Ampsim_set_param(a, AMP_PARAM_NEVE, neve);
    Ampsim_set_param(a, AMP_PARAM_CAB, cab);
    Ampsim_set_param(a, AMP_PARAM_PRESENCE, presence);

    float* L = (float*)malloc(n * sizeof(float));
    float* R = (float*)malloc(n * sizeof(float));
    if (!L || !R) { fprintf(stderr, "alloc failed\n"); return 1; }
    for (unsigned i = 0; i < n; ++i) {
        float o = 0.0f;
        Ampsim_process(a, in[i], &o);
        L[i] = o; R[i] = o;
    }
    /* normalize to 0.89 peak so loud presets never clip 16-bit render */
    float pk = peak_of(L, n);
    if (pk > 0.89f) {
        float g = 0.89f / pk;
        for (unsigned i = 0; i < n; ++i) { L[i] *= g; R[i] *= g; }
        pk = 0.89f;
    }
    printf("out: peak=%.3f rms=%.4f g=%.2f b=%.2f m=%.2f t=%.2f M=%.2f L=%.2f N=%.2f C=%.2f P=%.2f -> %s\n",
           pk, rms_of(L, n), gain, bass, mid, treble, master, level, neve, cab, presence, argv[2]);
    int rc = write_wav(argv[2], L, R, n, rate);
    free(L); free(R); free(mem); free(in);
    return rc;
}

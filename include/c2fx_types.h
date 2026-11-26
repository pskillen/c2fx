// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The c2fx Authors
/* Members are private and may change in any release. Callers only
 * declare and pass decoder state and scratch objects.
 */

#ifndef C2FX_TYPES_H
#define C2FX_TYPES_H

#include <stdint.h>

#define C2FX_LSP_ORDER 10
#define C2FX_MAX_HARMONICS 80

typedef struct c2fx_indices {
    int v_a; /* 1 = sub-frame a voiced */
    int v_b; /* 1 = sub-frame b voiced */
    int i;   /* w0 index, 0..127 */
    int e;   /* energy index, 0..31 */
    int d[C2FX_LSP_ORDER]; /* LSP delta indices, d[0] is d1 */
} c2fx_indices;

/* One sub-frame after dequant and the envelope.
 * lsp_q1 is Hz times 2 (the 0.5 Hz unit). w0 is the phase step, 2^32 = one
 * turn. energy_log2_q11 is log2 of the linear power. amp_log2_q11[m - 1] is
 * log2 of harmonic m's amplitude in Q11; int16 saturation caps near 2^16
 * linear (about 96 dB), below waveform clip. pitch_key is the w0 index,
 * or the sum of the two indices when w0 was averaged. pitch_mid says which.
 * Band edges and the harmonic bin are exact rationals of that key.
 */
typedef struct c2fx_subframe {
    int voiced;
    uint32_t w0;
    int count;
    int pitch_key;
    int pitch_mid;
    int16_t energy_log2_q11;
    int16_t lsp_q1[C2FX_LSP_ORDER];
    int16_t amp_log2_q11[C2FX_MAX_HARMONICS];
} c2fx_subframe;

/* Previous sub-frame b. 24 bytes on this ABI (four flags, ten int16 LSPs). */
typedef struct c2fx_front {
    uint8_t ready;
    uint8_t prev_voiced;
    uint8_t prev_i;
    uint8_t prev_e;
    int16_t prev_lsp_q1[C2FX_LSP_ORDER];
} c2fx_front;

#define C2FX_HOP 80
#define C2FX_DFT 512

/* Persistent decoder state. ola[] is Q8 (linear sample times 256). */
struct c2fx_dec {
    c2fx_front front;
    c2fx_indices prev_idx;
    uint32_t phi1;
    uint32_t rng;
    int32_t ola[C2FX_HOP];
    uint8_t lost_run;
};

/* Temporary decoder buffers. */
struct c2fx_dec_scratch {
    /* Both envelope calls finish before synthesis writes work or spec.
     * The log arrays are dead at that point; the results live in sub.
     */
    union {
        struct {
            int32_t work[C2FX_DFT];
            int32_t spec[C2FX_DFT];
        } synth;
        struct {
            int32_t log_den[256];
            int32_t log_num[256];
            int32_t log_p[256];
        } envelope;
    } buffers;
    c2fx_subframe sub[2];
};

#define C2FX_ANALYSIS_CENTER 139
#define C2FX_ANALYSIS_LEN (2 * C2FX_ANALYSIS_CENTER + 1)

#define C2FX_PITCH_BEFORE 260
#define C2FX_PITCH_AFTER 160
#define C2FX_PITCH_SPAN (C2FX_PITCH_BEFORE + C2FX_PITCH_AFTER)
#define C2FX_PITCH_MAX_LAG 160
#define C2FX_PITCH_POWER_BINS 385
/* Original FFT grid, including the interpolation filter's guard bins. */
#define C2FX_PITCH_GRID_GUARD 8
#define C2FX_PITCH_GRID_LAST 104

/* LPC and pitch use the workspace sequentially. */
struct c2fx_enc_scratch {
    union {
        struct {
            int32_t windowed[C2FX_ANALYSIS_LEN];
            int64_t r[C2FX_LSP_ORDER + 1];
            int64_t a[C2FX_LSP_ORDER + 1];
            int64_t poly[C2FX_LSP_ORDER / 2 + 1];
        } lpc;
        struct {
            union {
                int32_t fft[C2FX_DFT];
                int64_t energy_by_lag[C2FX_PITCH_MAX_LAG + 1];
            } transform;
            int32_t notch[367];
            union {
                int64_t fir[64];
                uint32_t power[130];
            } stage;
        } coarse;
        struct {
            int32_t power[C2FX_PITCH_POWER_BINS];
            union {
                int32_t hann[C2FX_ANALYSIS_CENTER + 1];
                int32_t grid[4 * (C2FX_PITCH_GRID_GUARD + C2FX_PITCH_GRID_LAST) + 2];
            } interp;
            int32_t spectrum[C2FX_DFT];
            int spectrum_shift;
        } refinement;
    } work;
};

union c2fx_scratch {
    struct c2fx_dec_scratch dec;
    struct c2fx_enc_scratch enc;
};

typedef struct c2fx_dec c2fx_dec_t;
typedef union c2fx_scratch c2fx_scratch_t;

#endif

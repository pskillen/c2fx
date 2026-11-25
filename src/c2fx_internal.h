// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The c2fx Authors
// Decoder front half. Nothing here is in the public include/c2fx.h yet.

#ifndef C2FX_INTERNAL_H
#define C2FX_INTERNAL_H

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
 * log2 of harmonic m's amplitude. pitch_key / pitch_mid select the band
 * edges: a grid index, or the sum of the two indices when w0 was averaged.
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

/* Gray-decoded field indices. Every 8-byte pattern is a frame. */
void c2fx_frame_unpack(const uint8_t in[8], c2fx_indices *idx);

void c2fx_front_init(c2fx_front *st);

/* Voicing, w0, energy and LSPs for sub-frame a (out[0]) and b (out[1]). */
void c2fx_dequant_frame(c2fx_front *st, const c2fx_indices *idx, c2fx_subframe out[2]);

/* LPC from LSPs. wide[k] / 2^q is coefficient k. q starts at 27 and drops
 * when the recursion would leave int32. |a| on real frames exceeds 8, which
 * a saturated Q12 copy cannot hold.
 */
void c2fx_lsp_to_lpc(
    const int16_t lsp_q1[C2FX_LSP_ORDER],
    int32_t wide[C2FX_LSP_ORDER + 1],
    int *q
);

/* L and log2 amplitudes. Calls the LSP to LPC step. */
void c2fx_envelope(c2fx_subframe *sub);

#endif

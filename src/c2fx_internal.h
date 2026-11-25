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
 * log2 of harmonic m's amplitude in Q11; int16 saturation caps near 2^16
 * linear (about 96 dB), below waveform clip. pitch_key / pitch_mid select the band
 * edges: a grid index, or the sum of the two indices when w0 was averaged.
 */
typedef struct c2fx_subframe {
    int voiced;
    uint32_t w0;
    int count;
    int pitch_key;
    int pitch_mid;
    /* When pitch_mid is set, pitch_prev is the previous frame's w0 index
     * and pitch_key is the sum of the two indices. The split matters for
     * the double-rounded harmonic bin; the sum alone does not.
     */
    int pitch_prev;
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

/* LPC from LSPs. wide[k] / 2^q is coefficient k. Q27 starts the scale; q
 * shifts down to Q21..Q25 on real frames. |a| on real frames exceeds 8, which
 * a saturated Q12 copy cannot hold.
 */
void c2fx_lsp_to_lpc(
    const int16_t lsp_q1[C2FX_LSP_ORDER],
    int32_t wide[C2FX_LSP_ORDER + 1],
    int *q
);

/* log2(1e-4) in Q11. round(log2(1e-4) * 2048) = -27213. Added to each
 * spectrum sum before the rescale.
 */
#define C2FX_SUM_EPS_LOG2_Q11 (-27213)

/* L and log2 amplitudes. Calls the LSP to LPC step. */
void c2fx_envelope(c2fx_subframe *sub);

/* log2 of (sum 2^v[k] + 2^extra), with v and extra in Q11. The envelope
 * passes C2FX_SUM_EPS_LOG2_Q11 as extra for the spectrum sums.
 */
int32_t c2fx_log2_sum(const int32_t *v, int n, int32_t extra);

/* Spec 8. One sub-frame hop, and the overlap tail. */
#define C2FX_HOP 80
#define C2FX_DFT 512

/* Same seed as the float model's C2M_PHASE_SEED, so both draw the same
 * xorshift32 sequence.
 */
#define C2FX_PHASE_SEED 0xC2F03200u

/* Spec 9, the float model's constants. 2 energy steps is 3.125 dB.
 * 4 frames is 80 ms, then one tail flush and silence.
 */
#define C2FX_LOST_ENERGY_STEPS 2
#define C2FX_LOST_MAX_FRAMES 4

/* Persistent decoder. The public object is a uint64_t array of this size,
 * rounded up in include/c2fx.h. ola[] is Q8 (linear sample times 256).
 */
typedef struct c2fx_dec_state {
    c2fx_front front;
    c2fx_indices prev_idx;
    uint32_t phi1;
    uint32_t rng;
    int32_t ola[C2FX_HOP];
    uint8_t lost_run;
} c2fx_dec_state;

void c2fx_dec_state_init(c2fx_dec_state *st);

/* log2(1 / (|A|^2 + 1e-6)) at the 256 bins, Q11. */
void c2fx_lpc_log_inv_power(const int16_t lsp_q1[C2FX_LSP_ORDER], int32_t log2_q11[256]);

/* Minimum phase of that log-power spectrum.
 * log2_q11 is bins 0..255. fft and side are 512 long and distinct.
 * side may alias log2_q11's buffer. On return, side[] is the complex log
 * in c2fx_rfft512 packing, and *sh is the cepstrum right shift.
 */
void c2fx_min_phase(const int32_t log2_q11[256], int32_t *fft, int32_t *side, int *sh);

/* Phase at one bin, uint32, 2^32 = one turn. Bin 0 and bin 256 are real. */
uint32_t c2fx_min_phase_bin(const int32_t *spec, int bin, int sh);

/* phi1 += 80 * w0 when voiced. w0 is the per-sample phase step. */
void c2fx_exc_advance(uint32_t *phi1, uint32_t w0, int voiced);

/* Harmonic m (1..L). Unvoiced draws the next xorshift phase. */
uint32_t c2fx_exc_harmonic(uint32_t phi1, uint32_t *rng, int voiced, int m);

/* One hop: minimum phase, inverse FFT, triangle, overlap-add.
 * work and spec are 512 int32 scratch buffers. pcm receives 80 samples.
 */
void c2fx_synth_hop(
    const c2fx_subframe *sub,
    int32_t *work,
    int32_t *spec,
    uint32_t *phi1,
    uint32_t *rng,
    int32_t ola[C2FX_HOP],
    int16_t *pcm);

/* Overlap-add one hop. y[] is 512 samples in Q8, centered as spec 8. */
void c2fx_ola_q8(int32_t ola[C2FX_HOP], const int32_t y[C2FX_DFT], int16_t out[C2FX_HOP]);

/* 1: idx is the repeated frame, synthesize it.
 * 0: do not synthesize. *flush is 1 for the tail frame, 0 for silence.
 * The caller then calls c2fx_lost_restore.
 */
int c2fx_lost_take(c2fx_dec_state *st, c2fx_indices *idx, int *flush);

/* Start-up state, keeping the noise generator. */
void c2fx_lost_restore(c2fx_dec_state *st);

#endif

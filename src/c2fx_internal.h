// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The c2fx Authors

#ifndef C2FX_INTERNAL_H
#define C2FX_INTERNAL_H

#include "c2fx_types.h"

/* Gray-decoded field indices. Every 8-byte pattern is a frame. */
void c2fx_frame_unpack(const uint8_t in[8], c2fx_indices *idx);

#ifndef C2FX_NO_ENCODER
void c2fx_frame_pack(const c2fx_indices *idx, uint8_t out[8]);
#endif

void c2fx_front_init(c2fx_front *st);

/* Voicing, w0, energy and LSPs for sub-frame a (out[0]) and b (out[1]). */
void c2fx_dequant_frame(c2fx_front *st, const c2fx_indices *idx, c2fx_subframe out[2]);

/* LPC analysis result. Energy is signed log2(G^2) in Q24, within
 * [-93, 22) for nonzero power, or INT32_MIN for zero. LSPs are
 * frequencies in Q16 Hz, from 0 to 4000 Hz.
 */
typedef struct c2fx_enc_row {
    int32_t energy_log2_q24;
    int32_t lsp_hz_q16[C2FX_LSP_ORDER];
} c2fx_enc_row;

/* One 10 ms row, with the 279-sample window centered at x[139].
 * The caller zero-pads outside the input.
 */
void c2fx_lpc_analyze(const int16_t x[C2FX_ANALYSIS_LEN],
                      c2fx_enc_row *row, struct c2fx_enc_scratch *s);

/* Levinson recursion on corrected, normalized lags with 0 < r[0] < 2^53
 * and |r[k]| <= r[0]. Order is 1..C2FX_LSP_ORDER. Both buffers have
 * order + 1 entries, and a[1..order] start at zero. Returns the residual
 * in the lag scale and writes the LPC coefficients in Q52.
 */
uint64_t c2fx_levinson_q52(const int64_t *r, int64_t *a, int order);

/* Pitch input spans center - 260 through center + 159, zero-padded by
 * the caller. The notch block spans [-160, 159], the period check uses
 * [-100, 99] at lags up to 160, and the analysis window spans [-139, 139].
 * Previous pitch is Q20 Hz, or zero when there is no voiced predecessor.
 * Coarse pitch is Q20 Hz before the final range clamp.
 */
int32_t c2fx_pitch_coarse(const int16_t x[C2FX_PITCH_SPAN], int32_t prev_hz_q20,
                          struct c2fx_enc_scratch *s);

/* Refined pitch, Q20 Hz in [50, 400]. The signed 32-bit array
 * s->work.refinement.spectrum holds the complex analysis FFT
 * in c2fx_rfft512 packing, with origin at the window center: retained
 * bin k = model/c2m_voicing.c bin k * exp(+j*2*pi*k*139/512).
 * The input is x times the Q38 Hann table divided by 2^spectrum_shift;
 * window products are rounded. LPC leaves spectrum and shift intact,
 * and 3340 bytes below the spectrum are free while it lives. Another pitch
 * row overwrites both. Bins 0..89 have complex error at most
 * max(96 counts, 0.005% of the in-band peak); measured worst 0.0031%.
 */
static inline int c2fx_pitch_spectrum_shift(const struct c2fx_enc_scratch *s)
{
    return s->work.refinement.spectrum_shift;
}

int32_t c2fx_pitch_hz(const int16_t x[C2FX_PITCH_SPAN], int32_t prev_hz_q20,
                      struct c2fx_enc_scratch *s);

/* Signal and residual in squared retained FFT counts after a 12-bit
 * shift of each component. Both terms share 2^(2*(spectrum_shift-26))
 * as the conversion to squared model FFT units. Pitch is Q20 Hz
 * in [50, 400]. Values outside that range return zero energy. Run after c2fx_pitch_hz on the same scratch. LPC analysis
 * may run on either side of voicing because it preserves the spectrum.
 */
typedef struct c2fx_voicing_energy {
    uint64_t signal;
    uint64_t error;
} c2fx_voicing_energy;

extern const int16_t c2fx_voicing_window_dtft[104];

c2fx_voicing_energy c2fx_voicing_snr(int32_t hz_q20, const struct c2fx_enc_scratch *s);
int c2fx_voicing_snr_pass(c2fx_voicing_energy energy);

/* Tilt decision bands for r1/r0: <= 0, (0, 0.5], (0.5, 0.6],
 * and > 0.6. The input is the same centered PCM window as LPC. Tilt
 * rebuilds the Hann taps in the dead refinement workspace below the FFT.
 * Pitch -> LPC -> voicing and pitch -> voicing -> LPC are both safe.
 */
typedef enum c2fx_voicing_tilt_band {
    C2FX_TILT_REJECT,
    C2FX_TILT_POSITIVE,
    C2FX_TILT_BRIDGE,
    C2FX_TILT_ACCEPT
} c2fx_voicing_tilt_band;

c2fx_voicing_tilt_band c2fx_voicing_tilt(const int16_t x[C2FX_ANALYSIS_LEN], struct c2fx_enc_scratch *s);

/* Base inputs and all flag results are 0 or 1; tilt is a decision band. */
int c2fx_voicing_base(int snr_pass, c2fx_voicing_tilt_band tilt);
int c2fx_voicing_final(int prev_base, int base, int next_base, c2fx_voicing_tilt_band tilt);

int c2fx_quant_w0_index(int32_t hz_q20);
int c2fx_quant_energy_index(const c2fx_enc_row *row);
void c2fx_quant_lsp_indices(const c2fx_enc_row *row, int d[C2FX_LSP_ORDER]);

/* High-precision cosine for the LPC conversions (2^32 per turn). */
int32_t c2fx_cos_q30(uint32_t phase);

/* LPC from LSPs. wide[k] / 2^q is coefficient k. Q27 starts the scale; q
 * shifts down to Q21..Q25 on real frames. |a| on real frames exceeds 8, which
 * a saturated Q12 copy cannot hold. quad is 3 * (C2FX_LSP_ORDER + 3) words:
 * polynomials P and Q, then the quadratic product. It is scratch.
 */
void c2fx_lsp_to_lpc(
    const int16_t lsp_q1[C2FX_LSP_ORDER],
    int32_t wide[C2FX_LSP_ORDER + 1],
    int *q,
    int32_t quad[3 * (C2FX_LSP_ORDER + 3)]
);

/* log2(1e-4) in Q11. round(log2(1e-4) * 2048) = -27213. Added to each
 * spectrum sum before the rescale.
 */
#define C2FX_SUM_EPS_LOG2_Q11 (-27213)

/* L and log2 amplitudes. Calls the LSP to LPC step.
 * log_den, log_num and log_p are 256-bin spectra; the caller owns them.
 * On return log_den is log2(1 / (|A|^2 + 1e-6)), Q11, which synthesis reads.
 */
void c2fx_envelope(
    c2fx_subframe *sub,
    int32_t log_den[256],
    int32_t log_num[256],
    int32_t log_p[256]);

/* log2 of (sum 2^v[k] + 2^extra), with v and extra in Q11. The envelope
 * passes C2FX_SUM_EPS_LOG2_Q11 as extra for the spectrum sums.
 */
int32_t c2fx_log2_sum(const int32_t *v, int n, int32_t extra);

/* Same seed as the float model's C2M_PHASE_SEED, so both draw the same
 * xorshift32 sequence.
 */
#define C2FX_PHASE_SEED 0xC2F03200u

/* Spec 9, the float model's constants. 2 energy steps is 3.125 dB.
 * 4 frames is 80 ms, then one tail flush and silence.
 */
#define C2FX_LOST_ENERGY_STEPS 2
#define C2FX_LOST_MAX_FRAMES 4

void c2fx_dec_state_init(c2fx_dec_t *st);

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
 * On entry work[0..255] is log2(1 / (|A|^2 + 1e-6)) in Q11 for this
 * sub-frame: the log_den that c2fx_envelope just left, when work is the
 * start of the same union (c2fx_dec_scratch). Nothing else of work is read.
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
int c2fx_lost_take(c2fx_dec_t *st, c2fx_indices *idx, int *flush);

/* Start-up state, keeping the noise generator. */
void c2fx_lost_restore(c2fx_dec_t *st);

#endif

// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The c2fx Authors
// Fixed-point helpers with RadioFoundry's mbe_* semantics: interpolated
// cosine, log2 and exp2 in Q11, and an unscaled 512-point real FFT.

#ifndef C2FX_DSP_H
#define C2FX_DSP_H

#include <stdint.h>

/* sin(pi/2 * i / 256), Q15 with full scale 32767. */
extern const int16_t c2fx_sin_tab[257];
/* 2^(14 + i/64), i = 0..64. */
extern const uint16_t c2fx_exp2_tab[65];
/* 32768 * log2(1 + i/64), i = 0..64. */
extern const uint16_t c2fx_log2_tab[65];

/* External DSP builds supply the five hooks below. */

/* Cosine of an unsigned phase, 2^32 = one turn, Q15 with full scale 32767.
 * Uses the top 10 phase bits for the table index and the lower 22 for
 * linear interpolation, rounding the increment down. Matches mbe_cos_i
 * and its quarter-wave mbe_sin_tab.
 */
int32_t c2fx_cos(uint32_t phase);

/* 2^(x/2048) for signed Q11 x. Linearly interpolates a Q14 mantissa
 * over 64 intervals, truncates the integer result down, and caps it at
 * 2^30. Negative x returns zero. Matches mbe_exp2_q11.
 */
int32_t c2fx_exp2_q11(int32_t x);

/* log2 of an unsigned integer, in Q11. Linearly interpolates the
 * normalized mantissa over 64 intervals and truncates down to Q11.
 * Zero returns -64 * 2048. Matches mbe_log2_q11.
 */
int32_t c2fx_log2_q11(uint32_t x);

/* In-place, unscaled transform of 512 signed int32 time samples.
 * X[k] = sum_n x[n] exp(-i 2 pi k n / 512). The output packs real DC
 * and Nyquist in buf[0] and buf[1], then real/imaginary pairs for bins
 * 1 through 255 in buf[2*k] and buf[2*k+1]. Q15 twiddle products and
 * the real-spectrum half sums use arithmetic right shifts. Matches mbe_rfft.
 * The largest uniform input magnitude safe for all add/subtract stages
 * is 4,194,303. c2fx_min_phase bounds the log spectrum by 204,099 and
 * normalizes the cepstrum to at most 2^20 before its second transform.
 */
void c2fx_rfft512(int32_t *buf);

/* In-place inverse from the same packed spectrum to 512 signed int32
 * time samples. Uses a positive exponent and the same Q15 twiddle
 * product rounding. The unnormalized inverse gives an ideal round-trip
 * gain of 512. Matches mbe_irfft.
 * The largest uniform input magnitude safe for all add/subtract stages
 * is 3,251,832 per packed word. Sparse spectra can have larger entries.
 * Synthesis places at most 80 harmonic contributions, each below 2^22
 * per component, with a separate real Nyquist contribution below 2^23.
 * The inverse preprocessing contributes at most four times the sum of
 * interior complex magnitudes plus twice the Nyquist magnitude, apart
 * from Q15 rounding. The complex FFT only rotates and sums these terms.
 * This bounds the decoder's inverse stages below 2.04e9, including
 * fewer than 2,000 counts of accumulated rounding error.
 */
void c2fx_irfft512(int32_t *buf);

#endif

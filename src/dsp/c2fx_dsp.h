// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The c2fx Authors
// Fixed-point helpers with RadioFoundry's mbe_* semantics: interpolated
// cosine, log2 and exp2 in Q11, and an unscaled 512-point real FFT.

#ifndef C2FX_DSP_H
#define C2FX_DSP_H

#include <stdint.h>

/* sin(pi/2 * i / 256), Q15. Entry 256 is 32767 (1.0 does not fit). */
extern const int16_t c2fx_sin_tab[257];
/* 2^(14 + i/64), i = 0..64. */
extern const uint16_t c2fx_exp2_tab[65];
/* 32768 * log2(1 + i/64), i = 0..64. */
extern const uint16_t c2fx_log2_tab[65];

/* cos of a phase, 2^32 = one turn, Q15, linear interpolation of the
 * quarter-wave table (RadioFoundry mbe_cos_i).
 */
int32_t c2fx_cos(uint32_t phase);

/* 2^(x/2048) as an integer. Saturates at 2^30. Values below 1 become 0. */
int32_t c2fx_exp2_q11(int32_t x);

/* log2(x) in Q11. x = 0 returns -(64 << 11). */
int32_t c2fx_log2_q11(uint32_t x);

/* In-place 512-point real FFT, unscaled. Half spectrum, re/im interleaved.
 * X[0] is buf[0] (real). X[256].re is buf[1]. X[k] for k = 1..255 is the
 * pair buf[2k], buf[2k+1]. X[k] = sum_n x[n] exp(-i 2 pi k n / 512).
 */
void c2fx_rfft512(int32_t *buf);

/* Inverse of c2fx_rfft512, same layout, unscaled. */
void c2fx_irfft512(int32_t *buf);

#endif

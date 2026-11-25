// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Copyright (c) 2026 The c2fx Authors
// 32x32 products. Callers stay in 32-bit accumulators; a wide product lives
// only inside these helpers (RadioFoundry fxp_mulshr / fxp_mulshr16).

#ifndef C2FX_FXP_H
#define C2FX_FXP_H

#include <stdint.h>

/* (a * b) >> s, the low 32 bits of the shifted product. 0 <= s < 32. */
static inline int32_t c2fx_mulshr(int32_t a, int32_t b, int s)
{
    return (int32_t)(uint32_t)((uint64_t)((int64_t)a * b) >> s);
}

/* (x * c) >> s for a 16-bit factor c, 0 <= s <= 16. Two 32-bit products.
 * c is a Q15 twiddle or gain, so |c| <= 32768.
 */
static inline int32_t c2fx_mulshr16(int32_t x, int32_t c, int s)
{
    return (int32_t)(((uint32_t)((x >> 16) * c) << (16 - s)) +
                     (uint32_t)(((int32_t)(x & 0xFFFF) * c) >> s));
}

/* Full unsigned 32x32 product. */
static inline uint64_t c2fx_umul32(uint32_t a, uint32_t b)
{
    return (uint64_t)a * (uint64_t)b;
}

/* trunc(x / d) for 32-bit x; inv = floor(2^32 / d) + 1 (d in a comment). */
static inline uint32_t c2fx_udiv32_inv(uint32_t x, uint32_t inv)
{
    return (uint32_t)(c2fx_umul32(x, inv) >> 32);
}

/* 64x64->128 product, high half only (portable). */
static inline uint64_t c2fx_umul64_hi(uint64_t a, uint64_t b)
{
    const uint64_t a0 = a & UINT64_C(0xffffffff);
    const uint64_t a1 = a >> 32;
    const uint64_t b0 = b & UINT64_C(0xffffffff);
    const uint64_t b1 = b >> 32;
    const uint64_t p00 = a0 * b0;
    const uint64_t p01 = a0 * b1;
    const uint64_t p10 = a1 * b0;
    const uint64_t p11 = a1 * b1;
    uint64_t mid = (p00 >> 32) + (p01 & UINT64_C(0xffffffff)) + (p10 & UINT64_C(0xffffffff));

    return p11 + (p01 >> 32) + (p10 >> 32) + (mid >> 32);
}

/* trunc(x / d) for 64-bit x; inv = floor(2^64 / d) + 1. */
static inline uint32_t c2fx_udiv64_inv(uint64_t x, uint64_t inv)
{
    return (uint32_t)c2fx_umul64_hi(x, inv);
}

/* trunc(x / (d * 2^30)) for 64-bit x; inv = floor(2^64 / d) + 1. */
static inline uint32_t c2fx_udiv_p30_inv(uint64_t x, uint64_t inv)
{
    return (uint32_t)(c2fx_umul64_hi(x, inv) >> 30);
}

#endif

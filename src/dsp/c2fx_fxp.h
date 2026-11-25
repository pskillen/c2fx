// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Copyright (c) 2026 The c2fx Authors
// 32x32 products. Callers stay in 32-bit accumulators; a wide product lives
// only inside these helpers (RadioFoundry fxp_mulshr / fxp_mulshr16).

#ifndef C2FX_FXP_H
#define C2FX_FXP_H

#include <stdint.h>

/* Leading-zero counts. All variants require x != 0. */
#if !defined(__GNUC__) || defined(C2FX_TEST_PORTABLE_CLZ)
static inline int c2fx_clz32_portable(uint32_t x)
{
    int count = 0;

    while ((x & UINT32_C(0x80000000)) == 0u) {
        x <<= 1;
        count++;
    }
    return count;
}

static inline int c2fx_clz64_portable(uint64_t x)
{
    uint32_t hi = (uint32_t)(x >> 32);

    if (hi != 0u) {
        return c2fx_clz32_portable(hi);
    }
    return 32 + c2fx_clz32_portable((uint32_t)x);
}
#endif

static inline int c2fx_clz32(uint32_t x)
{
#if defined(__GNUC__)
    return __builtin_clz(x);
#else
    return c2fx_clz32_portable(x);
#endif
}

static inline int c2fx_clz64(uint64_t x)
{
#if defined(__GNUC__)
    return __builtin_clzll(x);
#else
    return c2fx_clz64_portable(x);
#endif
}

/* (a * b) >> s, the low 32 bits of the shifted product. 0 <= s < 32. */
static inline int32_t c2fx_mulshr(int32_t a, int32_t b, int s)
{
    return (int32_t)(uint32_t)((uint64_t)((int64_t)a * b) >> s);
}

/* Bias the product away from zero by half a step, then floor the shift.
 * An exact negative multiple becomes one count more negative.
 * 1 <= s < 32, and the result must fit in int32_t.
 */
static inline int32_t c2fx_mulshr_bias_floor(int32_t a, int32_t b, int s)
{
    int64_t p = (int64_t)a * b;
    int64_t bias = (int64_t)1 << (s - 1);

    p += p < 0 ? -bias : bias;
    return (int32_t)(uint32_t)((uint64_t)p >> s);
}

/* Shift the product to nearest, with ties away from zero. Return its
 * low 32 bits for phase arithmetic. 1 <= s < 32.
 */
static inline uint32_t c2fx_mulshr_round(int32_t a, int32_t b, int s)
{
    int64_t p = (int64_t)a * b;
    uint64_t mag = (uint64_t)(p < 0 ? -p : p);
    uint64_t bias = UINT64_C(1) << (s - 1);
    uint32_t rounded = (uint32_t)((mag + bias) >> s);

    return p < 0 ? 0u - rounded : rounded;
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

/* floor(2^64 / d) + 1. The LPC cosine is the only caller.
 * 156 is the denominator on theta^2. 250 is the phase step.
 * 6, 20, 42, 72 and 110 are the other Taylor denominators; those
 * products are divided by d * 2^30.
 */
#define C2FX_INV_U64_156 UINT64_C(118248359446856101)
#define C2FX_INV_U64_250 UINT64_C(73786976294838207)
#define C2FX_INV_U64_P30_6 UINT64_C(3074457345618258603)
#define C2FX_INV_U64_P30_20 UINT64_C(922337203685477581)
#define C2FX_INV_U64_P30_42 UINT64_C(439208192231179801)
#define C2FX_INV_U64_P30_72 UINT64_C(256204778801521551)
#define C2FX_INV_U64_P30_110 UINT64_C(167697673397359561)

/* trunc(x / d) for a 32-bit numerator.
 * inv = floor(2^64 / d) + 1 with d in 1..65535. Exact for every uint32
 * x: the multiplier exceeds 2^64/d by at most d, and x * d stays below
 * 2^48, so the estimate never crosses another integer. The cosine
 * series calls this for d = 156 with x = theta^2 in 0..2649351757.
 */
static inline uint32_t c2fx_udiv32_inv(uint32_t x, uint64_t inv)
{
    return (uint32_t)c2fx_umul64_hi((uint64_t)x, inv);
}

/* Low 32 bits of trunc(x / d).
 * inv = floor(2^64 / d) + 1. Exact while
 * x * (inv * d - 2^64) < 2^64, which keeps the estimate from crossing
 * an extra integer. For d = 250 that excess is 134, so the bound holds
 * for x < 2^64/134. The phase step feeds x = q1 * 2^26 + 125 with q1
 * in 0..32767 (x <= 2198956146813). One turn is 2^32, and these low
 * 32 bits are the phase.
 */
static inline uint32_t c2fx_udiv64_inv(uint64_t x, uint64_t inv)
{
    return (uint32_t)c2fx_umul64_hi(x, inv);
}

/* trunc(x / (d * 2^30)).
 * inv = floor(2^64 / d) + 1. The raw shift is exact or one too high
 * while x * (inv * d - 2^64) < d * 2^64 (the extra is under one unit
 * of the unscaled denominator). q * d > x >> 30 marks that
 * too-high case, and subtracting it once corrects it. Callers use d in
 * {6, 20, 42, 72, 110} and x <= 2649351757 * 2^30. On that range the
 * excess is at most 0.14 and q * d fits in uint64.
 */
static inline uint32_t c2fx_udiv_p30_inv(uint64_t x, uint64_t inv, uint32_t d)
{
    uint32_t q = (uint32_t)(c2fx_umul64_hi(x, inv) >> 30);

    if ((uint64_t)q * (uint64_t)d > (x >> 30)) {
        q -= 1u;
    }
    return q;
}

#endif

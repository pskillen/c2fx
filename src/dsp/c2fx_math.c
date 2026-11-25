// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Copyright (c) 2026 The c2fx Authors
// Interpolated cosine, 2^x and log2. Same results as RadioFoundry's
// mbe_cos_i, mbe_exp2_q11 and mbe_log2_q11.

#include "c2fx_dsp.h"

#ifndef C2FX_EXTERNAL_DSP

#include "c2fx_fxp.h"

static int32_t sin_idx(uint32_t i)
{
    uint32_t q = i & 255u;
    int32_t v = c2fx_sin_tab[(i & 256u) ? 256u - q : q];

    if ((i & 512u) != 0u) {
        v = -v;
    }
    return v;
}

int32_t c2fx_cos(uint32_t phase)
{
    uint32_t i = phase >> 22;
    int32_t c0 = sin_idx(i + 256u);
    int32_t c1 = sin_idx(i + 257u);

    /* One quarter-wave step is at most 201, so the product stays in 32 bits. */
    return c0 + (((c1 - c0) * (int32_t)(phase & 0x3FFFFFu)) >> 22);
}

int32_t c2fx_exp2_q11(int32_t x)
{
    int32_t ip;
    uint32_t fr;
    uint32_t i;
    uint32_t f;
    uint32_t m;

    /* Floor divide by 2048. A negative x uses the positive remainder so
     * x = ip * 2048 + fr with fr in 0..2047, matching an arithmetic shift.
     */
    if (x >= 0) {
        ip = x >> 11;
        fr = (uint32_t)x & 2047u;
    } else {
        uint32_t mag = (uint32_t)(-(x + 1)) + 1u;
        uint32_t whole = mag >> 11;
        uint32_t rem = mag & 2047u;

        if (rem == 0u) {
            ip = -(int32_t)whole;
            fr = 0u;
        } else {
            ip = -(int32_t)whole - 1;
            fr = 2048u - rem;
        }
    }

    i = fr >> 5;
    f = fr & 31u;
    /* Q14 mantissa in [1, 2). */
    m = (uint32_t)c2fx_exp2_tab[i] +
        (((uint32_t)(c2fx_exp2_tab[i + 1] - c2fx_exp2_tab[i]) * f) >> 5);

    if (ip >= 30) {
        return 1 << 30;
    }
    if (ip >= 14) {
        return (int32_t)(m << (ip - 14));
    }
    if (ip < -1) {
        return 0;
    }
    return (int32_t)(m >> (14 - ip));
}

int32_t c2fx_log2_q11(uint32_t x)
{
    int e;
    uint32_t m;
    uint32_t fr;
    uint32_t i;
    uint32_t f;
    uint32_t l;

    if (x == 0u) {
        return -(64 << 11);
    }
    e = 31 - c2fx_clz32(x);
    if (e >= 16) {
        m = x >> (e - 16);
    } else {
        m = x << (16 - e);
    }
    fr = m & 0xFFFFu;
    i = fr >> 10;
    f = fr & 1023u;
    /* Q15 fraction of the mantissa. */
    l = (uint32_t)c2fx_log2_tab[i] +
        (((uint32_t)(c2fx_log2_tab[i + 1] - c2fx_log2_tab[i]) * f) >> 10);
    return (e << 11) + (int32_t)(l >> 4);
}

#endif

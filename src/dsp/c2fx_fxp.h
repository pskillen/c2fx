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

#endif

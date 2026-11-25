// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The c2fx Authors
// Spec 5: LSP to LPC. The recursion stays at Q27 until a coefficient would
// leave int32, then the whole polynomial shifts right and the Q drops.
// The Q12 copy is what a later filter uses; |a| on real frames can exceed 8,
// so the envelope keeps the wide coefficients.

#include "c2fx_internal.h"

#include "c2fx_dsp.h"
#include "c2fx_fxp.h"

#define LPC_Q 27
#define LPC_ONE (1 << LPC_Q)

static uint32_t iabs32(int32_t x)
{
    if (x >= 0) {
        return (uint32_t)x;
    }
    return (uint32_t)(-(x + 1)) + 1u;
}

static uint32_t phase_from_q1(int q1)
{
    /* round(2^32 * (q1/2) / 8000) = round(q1 * 2^26 / 250). */
    return (uint32_t)((((uint64_t)q1 << 26) + 125u) / 250u);
}

static void scale_down(int32_t *poly, int n, int *q)
{
    uint32_t peak = 0;

    for (int i = 0; i <= n; i++) {
        uint32_t a = iabs32(poly[i]);

        if (a > peak) {
            peak = a;
        }
    }
    /* |c1| <= 2, so the next coefficient is at most 4 * peak. Keep that
     * under 2^29 so two polynomials can still be added in int32.
     */
    while (peak > (1u << 27)) {
        for (int i = 0; i <= n; i++) {
            poly[i] >>= 1;
        }
        peak >>= 1;
        *q -= 1;
    }
}

/* poly *= (1 + c1 * z^-1 + z^-2). c1 is Q15 and holds the true -2*cos. */
static void mul_quadratic(int32_t *poly, int *order, int *q, int32_t c1_q15)
{
    int n = *order;
    int32_t next[C2FX_LSP_ORDER + 3];

    scale_down(poly, n, q);
    for (int i = 0; i <= n + 2; i++) {
        next[i] = 0;
    }
    for (int i = 0; i <= n; i++) {
        int32_t step = c2fx_mulshr(poly[i], c1_q15, 15);

        next[i] += poly[i];
        next[i + 1] += step;
        next[i + 2] += poly[i];
    }
    *order = n + 2;
    for (int i = 0; i <= *order; i++) {
        poly[i] = next[i];
    }
}

static void align_q(int32_t *poly, int n, int *q, int target)
{
    while (*q > target) {
        for (int i = 0; i <= n; i++) {
            poly[i] >>= 1;
        }
        *q -= 1;
    }
}

static int16_t to_q12(int32_t value, int q)
{
    int32_t v = value;

    if (q > 12) {
        int sh = q - 12;
        int32_t bias = 1 << (sh - 1);

        if (v >= 0) {
            v = (v + bias) >> sh;
        } else {
            v = -((-v + bias) >> sh);
        }
    } else if (q < 12) {
        v <<= (12 - q);
    }
    if (v > 32767) {
        return 32767;
    }
    if (v < -32768) {
        return -32768;
    }
    return (int16_t)v;
}

void c2fx_lsp_to_lpc(
    const int16_t lsp_q1[C2FX_LSP_ORDER],
    int16_t a_q12[C2FX_LSP_ORDER + 1],
    int32_t wide[C2FX_LSP_ORDER + 1],
    int *q_out
)
{
    int32_t p[C2FX_LSP_ORDER + 3];
    int32_t q[C2FX_LSP_ORDER + 3];
    int p_order = 1;
    int q_order = 1;
    int p_q = LPC_Q;
    int q_q = LPC_Q;

    p[0] = LPC_ONE;
    p[1] = LPC_ONE;
    q[0] = LPC_ONE;
    q[1] = -LPC_ONE;
    for (int k = 2; k <= C2FX_LSP_ORDER + 2; k++) {
        p[k] = 0;
        q[k] = 0;
    }

    for (int j = 0; j < C2FX_LSP_ORDER; j += 2) {
        int32_t cos_q15 = c2fx_cos(phase_from_q1(lsp_q1[j]));

        mul_quadratic(p, &p_order, &p_q, -(cos_q15 << 1));
    }
    for (int j = 1; j < C2FX_LSP_ORDER; j += 2) {
        int32_t cos_q15 = c2fx_cos(phase_from_q1(lsp_q1[j]));

        mul_quadratic(q, &q_order, &q_q, -(cos_q15 << 1));
    }

    if (p_q > q_q) {
        align_q(p, C2FX_LSP_ORDER, &p_q, q_q);
    } else if (q_q > p_q) {
        align_q(q, C2FX_LSP_ORDER, &q_q, p_q);
    }

    for (int k = 0; k <= C2FX_LSP_ORDER; k++) {
        /* (P + Q) / 2. The z^-11 terms cancel; order 10 remains. */
        int32_t sum = p[k] + q[k];
        int32_t ak = sum >> 1;

        wide[k] = ak;
        a_q12[k] = to_q12(ak, p_q);
    }
    *q_out = p_q;
}

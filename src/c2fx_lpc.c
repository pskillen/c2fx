// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The c2fx Authors
// Spec 5: LSP to LPC. The recursion stays at Q27 until a coefficient would
// leave int32, then the whole polynomial shifts right and the Q drops.
// Callers keep that Q. A Q12 copy saturates: |a| on real frames exceeds 8.

#include "c2fx_internal.h"

#define LPC_Q 27
#define LPC_ONE (1 << LPC_Q)

/* sin(z * pi/2 / 2^30) in Q30, for z in 0..2^30. Horner form of the Taylor
 * series through theta^13:
 *   sin/theta = 1 - t/6*(1 - t/20*(1 - t/42*(1 - t/72*(1 - t/110*(1 - t/156)))))
 * with t = theta^2. The truncation error is about 2e-9.
 * c2fx_cos is not used here. It is Q15 and about one LSB low, and the
 * product truncates toward -infinity. On equally spaced LSPs (silence)
 * P and Q cancel to a residual near 2e-4; that LSB moves the DC bin, the
 * bin dominates both power sums, and every harmonic comes out 0.22 dB high.
 */
static int32_t sin_quadrant_q30(uint32_t z)
{
    const uint64_t pi_q30 = 3373259426ull;
    uint64_t th = ((uint64_t)z * pi_q30) >> 31;
    uint64_t th2 = (th * th) >> 30;
    uint64_t s = (1ull << 30) - th2 / 156ull;

    s = (1ull << 30) - (th2 * s) / (110ull << 30);
    s = (1ull << 30) - (th2 * s) / (72ull << 30);
    s = (1ull << 30) - (th2 * s) / (42ull << 30);
    s = (1ull << 30) - (th2 * s) / (20ull << 30);
    s = (1ull << 30) - (th2 * s) / (6ull << 30);
    return (int32_t)((th * s) >> 30);
}

/* cos(phase), 2^32 = one turn, Q30. */
static int32_t cos_q30(uint32_t phase)
{
    uint32_t quad = phase >> 30;
    uint32_t z = phase & 0x3fffffffu;
    int32_t mag;

    if ((quad & 1u) == 0u) {
        mag = sin_quadrant_q30((1u << 30) - z);
    } else {
        mag = sin_quadrant_q30(z);
    }
    if (quad == 1u || quad == 2u) {
        return -mag;
    }
    return mag;
}

/* (coeff * -2*cos) with cos in Q30. The shift is logical; the result fits
 * in int32 because |coeff| was scaled to 2^27 and |-2*cos| <= 2.
 */
static int32_t mul_cos(int32_t coeff, int32_t cos_q30)
{
    int64_t c1 = -(int64_t)cos_q30 * 2;

    return (int32_t)(uint32_t)((uint64_t)((int64_t)coeff * c1) >> 30);
}

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
    /* |-2*cos| <= 2, so the next coefficient is at most 4 * peak. Keep
     * that under 2^29 so two polynomials can still be added in int32.
     */
    while (peak > (1u << 27)) {
        for (int i = 0; i <= n; i++) {
            poly[i] >>= 1;
        }
        peak >>= 1;
        *q -= 1;
    }
}

/* poly *= (1 - 2*cos*z^-1 + z^-2). cosine is Q30. */
static void mul_quadratic(int32_t *poly, int *order, int *q, int32_t cosine)
{
    int n = *order;
    int32_t next[C2FX_LSP_ORDER + 3];

    scale_down(poly, n, q);
    for (int i = 0; i <= n + 2; i++) {
        next[i] = 0;
    }
    for (int i = 0; i <= n; i++) {
        next[i] += poly[i];
        next[i + 1] += mul_cos(poly[i], cosine);
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

void c2fx_lsp_to_lpc(
    const int16_t lsp_q1[C2FX_LSP_ORDER],
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
        mul_quadratic(p, &p_order, &p_q, cos_q30(phase_from_q1(lsp_q1[j])));
    }
    for (int j = 1; j < C2FX_LSP_ORDER; j += 2) {
        mul_quadratic(q, &q_order, &q_q, cos_q30(phase_from_q1(lsp_q1[j])));
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
    }
    *q_out = p_q;
}

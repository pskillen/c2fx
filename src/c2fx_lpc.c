// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The c2fx Authors
// Spec 5: LSP to LPC. Q27 starts the block-float scale; q shifts down to Q21..Q25
// on real frames. Callers keep q. A Q12 copy saturates: |a| on real frames exceeds 8.

#include "c2fx_internal.h"

#include "dsp/c2fx_fxp.h"

#define LPC_Q 27
#define LPC_ONE (1 << LPC_Q)

#define PI_Q30 3373259426u /* pi in Q30 */

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
    uint32_t th = (uint32_t)(c2fx_umul32(z, PI_Q30) >> 31);
    uint32_t th2 = (uint32_t)c2fx_mulshr((int32_t)th, (int32_t)th, 30);
    uint32_t s = (1u << 30) - c2fx_udiv32_inv(th2, C2FX_INV_U64_156);

    s = (1u << 30) - c2fx_udiv_p30_inv(c2fx_umul32(th2, s), C2FX_INV_U64_P30_110, 110u);
    s = (1u << 30) - c2fx_udiv_p30_inv(c2fx_umul32(th2, s), C2FX_INV_U64_P30_72, 72u);
    s = (1u << 30) - c2fx_udiv_p30_inv(c2fx_umul32(th2, s), C2FX_INV_U64_P30_42, 42u);
    s = (1u << 30) - c2fx_udiv_p30_inv(c2fx_umul32(th2, s), C2FX_INV_U64_P30_20, 20u);
    s = (1u << 30) - c2fx_udiv_p30_inv(c2fx_umul32(th2, s), C2FX_INV_U64_P30_6, 6u);
    return c2fx_mulshr((int32_t)th, (int32_t)s, 30);
}

/* cos(phase), 2^32 = one turn, Q30. */
int32_t c2fx_cos_q30(uint32_t phase)
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

/* coeff * (-2 * cos), cos in Q30.
 * Forming -2 * cos in int32 overflows at cos = -1: an LSP of 4000 Hz
 * has phase 2^31 and cos = -2^30, so -2 * cos is 2^31. coeff * (-cos)
 * shifted by 29 is the same product, and -cos fits in int32.
 */
static int32_t mul_cos(int32_t coeff, int32_t cos_q30)
{
    return c2fx_mulshr(coeff, -cos_q30, 29);
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
    /* round(2^32 * (q1/2) / 8000) = (q1 * 2^26 + 125) / 250. */
    uint64_t num = c2fx_umul32((uint32_t)q1, 1u << 26) + 125u;

    return c2fx_udiv64_inv(num, C2FX_INV_U64_250);
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

/* poly *= (1 - 2*cos*z^-1 + z^-2). cosine is Q30.
 * next has C2FX_LSP_ORDER + 3 entries and is only written here.
 */
static void mul_quadratic(int32_t *poly, int *order, int *q, int32_t cosine, int32_t *next)
{
    int n = *order;

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
    int *q_out,
    int32_t quad[3 * (C2FX_LSP_ORDER + 3)]
)
{
    const int npoly = C2FX_LSP_ORDER + 3;
    int32_t *p = quad;
    int32_t *q = quad + npoly;
    int32_t *next = quad + 2 * npoly;
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
        mul_quadratic(p, &p_order, &p_q, c2fx_cos_q30(phase_from_q1(lsp_q1[j])), next);
    }
    for (int j = 1; j < C2FX_LSP_ORDER; j += 2) {
        mul_quadratic(q, &q_order, &q_q, c2fx_cos_q30(phase_from_q1(lsp_q1[j])), next);
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

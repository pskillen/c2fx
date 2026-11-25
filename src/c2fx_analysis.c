// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The c2fx Authors

#include "c2fx_internal.h"

#include "c2fx_window.h"
#include "dsp/c2fx_fxp.h"

#define LPC_Q 52
#define LPC_ONE (INT64_C(1) << LPC_Q)
#define WHITE_NOISE INT64_C(900719925) /* round(2e-7 * 2^52) */
#define RHO_MAX INT64_C(4503595123770868) /* round(0.999999 * 2^52) */
#define CORR_BIT 51
#define WINDOW_Q 20
#define LOG_Q 24
#define LSP_HALF_ORDER (C2FX_LSP_ORDER / 2)
#define PHASE_MARGIN 68357u /* 1e-4 radians in phase units */
#define NYQUIST_HZ_Q16 (UINT32_C(4000) << 16)

/* Windowed Q20 samples are smaller than 2^28 in magnitude. The Hann power
 * is 1/512, so full-scale input gives r[0] up to about 2^61 in Q40.
 * Cauchy-Schwarz gives the same bound for every partial lag sum.
 */
static int autocorrelation(const int16_t *x, struct c2fx_enc_scratch *s)
{
    uint32_t peak = 0;
    int boost;
    int32_t gain;

    /* Bring quiet PCM into the same working range before window rounding.
     * Endpoints have zero weight and do not affect the required scale.
     */
    for (int i = 1; i < C2FX_ANALYSIS_LEN - 1; i++) {
        int32_t sample = x[i];
        uint32_t mag = (uint32_t)(sample < 0 ? -sample : sample);

        if (mag > peak) {
            peak = mag;
        }
    }
    if (peak == 0) {
        s->r[0] = 0;
        return 0;
    }
    /* The top bit goes to bit 14. INT16_MIN already fills the range. */
    boost = peak == 32768u ? 0 : c2fx_clz32(peak) - 17;
    gain = 1 << boost;

    s->windowed[0] = 0;
    s->windowed[C2FX_ANALYSIS_LEN - 1] = 0;
    for (int i = 1; i < C2FX_ANALYSIS_LEN - 1; i++) {
        int k = i <= C2FX_ANALYSIS_CENTER ? i : C2FX_ANALYSIS_LEN - 1 - i;

        s->windowed[i] = (int32_t)c2fx_mulshr_round(x[i] * gain, c2fx_hann_q38[k], 38 - WINDOW_Q);
    }

    for (int k = 0; k <= C2FX_LSP_ORDER; k++) {
        int64_t sum = 0;

        for (int i = 0; i < C2FX_ANALYSIS_LEN - k; i++) {
            sum = c2fx_mac32(sum, s->windowed[i], s->windowed[i + k]);
        }
        s->r[k] = sum;
    }
    /* The boosted peak and every interior Hann tap are large enough
     * that r[0] is nonzero. Put its high bit at CORR_BIT to leave headroom
     * for the LPC sum. Each lag is the stored value times 2^(shift-40-2*boost).
     */
    int shift = 63 - c2fx_clz64((uint64_t)s->r[0]) - CORR_BIT;
    for (int k = 0; k <= C2FX_LSP_ORDER; k++) {
        if (shift > 0) {
            s->r[k] >>= shift;
        } else {
            s->r[k] *= INT64_C(1) << -shift;
        }
    }
    /* White-noise correction, 2e-7 in Q52. */
    s->r[0] = c2fx_add64(s->r[0], c2fx_mulshr64(s->r[0], WHITE_NOISE, LPC_Q));
    return shift - 2 * (WINDOW_Q + boost);
}

/* The remainder stays below e after each bit, so doubling fits. */
static int64_t reflection(int64_t acc, int64_t e)
{
    uint64_t rem = c2fx_abs64(acc);
    uint64_t den = (uint64_t)e;
    int64_t rho = RHO_MAX;

    if (rem < den) {
        rho = 0;
        for (int i = 0; i < LPC_Q; i++) {
            rem <<= 1;
            rho <<= 1;
            if (rem >= den) {
                rem -= den;
                rho |= 1;
            }
        }
        if (rho > RHO_MAX) {
            rho = RHO_MAX;
        }
    }
    return acc > 0 ? -rho : rho;
}

static uint64_t levinson(const int64_t *r, int64_t *a)
{
    int64_t e = r[0];

    a[0] = LPC_ONE;
    /* A reflection magnitude <= 1 bounds |a[j]| by binomial(i, j),
     * hence at most 252. The lag sum uses order <= 9, whose coefficient
     * magnitudes sum to at most 512 (apart from fixed-point rounding).
     */
    for (int i = 1; i <= C2FX_LSP_ORDER && e > 0; i++) {
        int64_t acc = r[i];
        int64_t rho;

        for (int j = 1; j < i; j++) {
            acc = c2fx_add64(acc, c2fx_mulshr64(a[j], r[i - j], LPC_Q));
        }
        rho = reflection(acc, e);
        for (int j = 1; j <= i / 2; j++) {
            int64_t left = a[j];
            int64_t right = a[i - j];

            a[j] = c2fx_add64(left, c2fx_mulshr64(rho, right, LPC_Q));
            a[i - j] = c2fx_add64(right, c2fx_mulshr64(rho, left, LPC_Q));
        }
        a[i] = rho;
        e = c2fx_mulshr64(e, LPC_ONE - c2fx_mulshr64(rho, rho, LPC_Q), LPC_Q);
    }
    return (uint64_t)e;
}

/* Normalize to [1, 2) in Q31. Squaring emits one fractional log2 bit
 * at a time: crossing 2 sets the bit and renormalizes the mantissa.
 */
static int32_t energy_log2(uint64_t energy, int exponent)
{
    int high = 63 - c2fx_clz64(energy);
    uint32_t mantissa = high > 31 ? (uint32_t)(energy >> (high - 31))
                                  : (uint32_t)energy << (31 - high);
    int32_t log = 0;

    for (int i = 0; i < LOG_Q; i++) {
        uint64_t square = c2fx_umul32(mantissa, mantissa);
        uint32_t bit = (uint32_t)(square >> 63);

        log = (log << 1) | (int32_t)bit;
        mantissa = (uint32_t)(square >> (31 + bit));
    }
    return (high + exponent) * (1 << LOG_Q) + log;
}

/* gamma^k in Q30, with gamma = exp(-pi * 15 / 8000). */
static const int32_t bandwidth_q30[C2FX_LSP_ORDER] = {
    1067435554, 1061166322, 1054933911, 1048738103, 1042578685,
    1036455441, 1030368161, 1024316632, 1018300645, 1012319991
};

static void flat_lsps(c2fx_enc_row *row, unsigned first, unsigned divisor)
{
    for (unsigned j = 0; j < C2FX_LSP_ORDER; j++) {
        row->lsp_hz_q16[j] = (int32_t)(((j + first) * NYQUIST_HZ_Q16 + divisor / 2) / divisor);
    }
}

/* P = (A + reverse(A)) / (1 + z^-1), Q uses the two minus signs.
 * Both are symmetric of order 10, so only the first six terms matter.
 * Store those in reverse order for Clenshaw, with the off-center terms
 * doubled. Q46 leaves room for the sums and polynomial evaluation.
 */
static void split_polynomial(const int64_t *a, int sign, int64_t *poly)
{
    int64_t prev = a[0] >> (LPC_Q - 46);

    poly[LSP_HALF_ORDER] = prev * 2;
    for (int k = 1; k <= LSP_HALF_ORDER; k++) {
        int64_t direct = a[k] >> (LPC_Q - 46);
        int64_t mirror = a[C2FX_LSP_ORDER + 1 - k] >> (LPC_Q - 46);

        prev = c2fx_add64(direct, sign * c2fx_add64(mirror, -prev));
        poly[LSP_HALF_ORDER - k] = k < LSP_HALF_ORDER ? prev * 2 : prev;
    }
}

static int64_t cosine_poly(const int64_t *poly, uint32_t phase)
{
    int32_t x = c2fx_cos_q30(phase);
    int64_t curr = 0;
    int64_t prev = 0;

    for (int k = LSP_HALF_ORDER; k > 0; k--) {
        int64_t next = c2fx_add64(poly[k], c2fx_add64(c2fx_mulshr64(curr, x, 29), -prev));

        prev = curr;
        curr = next;
    }
    return c2fx_add64(poly[0], c2fx_add64(c2fx_mulshr64(curr, x, 30), -prev));
}

static int crosses_zero(int64_t left, int64_t right)
{
    return left == 0 || (left < 0 && right > 0) || (left > 0 && right < 0);
}

static int32_t bisect_root(const int64_t *poly, uint32_t lo, uint32_t hi, int64_t left)
{
    if (left == 0) {
        hi = lo;
    }
    while (hi - lo > 1u) {
        uint32_t mid = lo + (hi - lo) / 2u;
        int64_t value = cosine_poly(poly, mid);

        if (value == 0 || crosses_zero(left, value)) {
            hi = mid;
        } else {
            lo = mid;
        }
    }

    /* phase * (4000 * 2^16) / 2^31 = phase * 125 / 1024. */
    return (int32_t)((c2fx_umul32(hi, 125u) + 512u) >> 10);
}

/* Try a coarse angular search first. Revisit the whole grid at the
 * model's resolution if a close pair fell in one interval.
 */
static int find_roots(const int64_t *poly, int32_t *hz, int grid_bits)
{
    uint32_t lo = PHASE_MARGIN;
    int64_t left = cosine_poly(poly, lo);
    unsigned count = 0;

    for (unsigned i = 1; i <= (1u << grid_bits); i++) {
        uint32_t hi = PHASE_MARGIN
            + (i << (31 - grid_bits)) - ((2u * PHASE_MARGIN * i) >> grid_bits);
        int64_t right = cosine_poly(poly, hi);

        if (crosses_zero(left, right)) {
            hz[count++] = bisect_root(poly, lo, hi, left);
            if (count == LSP_HALF_ORDER) {
                return 1;
            }
        }
        left = right;
        lo = hi;
    }
    return 0;
}

static void lpc_to_lsp(c2fx_enc_row *row, struct c2fx_enc_scratch *s)
{
    for (int k = 1; k <= C2FX_LSP_ORDER; k++) {
        s->a[k] = c2fx_mulshr64(s->a[k], bandwidth_q30[k - 1], 30);
    }

    for (int j = 0; j < 2; j++) {
        int32_t *hz = row->lsp_hz_q16 + j * LSP_HALF_ORDER;

        split_polynomial(s->a, 1 - 2 * j, s->poly);
        if (!find_roots(s->poly, hz, 7) && !find_roots(s->poly, hz, 12)) {
            flat_lsps(row, 1, C2FX_LSP_ORDER + 1);
            return;
        }
    }
    for (int j = LSP_HALF_ORDER; j < C2FX_LSP_ORDER; j++) {
        int32_t hz = row->lsp_hz_q16[j];
        int k = j;

        while (k > 0 && row->lsp_hz_q16[k - 1] > hz) {
            row->lsp_hz_q16[k] = row->lsp_hz_q16[k - 1];
            k--;
        }
        row->lsp_hz_q16[k] = hz;
    }
}

void c2fx_lpc_analyze(const int16_t x[C2FX_ANALYSIS_LEN],
                      c2fx_enc_row *row, struct c2fx_enc_scratch *s)
{
    int exponent = autocorrelation(x, s);
    uint64_t energy;

    if (s->r[0] == 0) {
        row->energy_log2_q24 = INT32_MIN;
        flat_lsps(row, 0, C2FX_LSP_ORDER);
        return;
    }
    for (int j = 1; j <= C2FX_LSP_ORDER; j++) {
        s->a[j] = 0;
    }
    energy = levinson(s->r, s->a);
    row->energy_log2_q24 = energy == 0 ? INT32_MIN : energy_log2(energy, exponent);
    lpc_to_lsp(row, s);
}

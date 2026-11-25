// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The c2fx Authors
// Spec 6, revision 5. |A|^2 at the 256 bins of a 512-point DFT, then the
// post filter in the log2 domain, the sum-preserving rescale, energy, bass
// boost, half-open band sums, square root and the first-harmonic scale.

#include "c2fx_internal.h"

#include "c2fx_dsp.h"
#include "c2fx_fxp.h"

#define SPECTRUM_BINS 256

/* 0.275 in Q15. round(0.275 * 32768) = 9011. */
#define C2FX_BETA_Q15 9011

/* log2(1.96) in Q11. round(log2(1.96) * 2048) = 1988. Real value 1.96. */
#define C2FX_BASS_LOG2_Q11 1988

/* log2(0.032) in Q11. round(log2(0.032) * 2048) = -10170. Real value 0.032. */
#define C2FX_HARM1_LOG2_Q11 (-10170)

/* 1e-6 = 1125899907 * 2^-50. Relative error about 1.4e-10. */
#define C2FX_POWER_FLOOR_MANT 1125899907u
#define C2FX_POWER_FLOOR_EXP (-50)

/* gamma = 0.6. gamma^k in Q15, k = 1..10. k = 0 is exactly 1 and is not
 * multiplied. round(0.6^k * 32768).
 */
static const int16_t gamma_q15[C2FX_LSP_ORDER + 1] = {
    32767, 19661, 11796, 7078, 4247, 2548, 1529, 917, 550, 330, 198
};

/* cos(2*pi/512) and sin(2*pi/512) in Q30.
 * Real values 0.999924701839 and 0.012271538286.
 */
#define ROT_COS_Q30 1073660973
#define ROT_SIN_Q30 13176464

static uint32_t iabs32(int32_t x)
{
    if (x >= 0) {
        return (uint32_t)x;
    }
    return (uint32_t)(-(x + 1)) + 1u;
}

/* |X|^2 = mant * 2^exp, mant in [2^30, 2^31) or 0. */
static void mag2_norm(int32_t re, int32_t im, uint32_t *mant, int *exp)
{
    uint32_t ar = iabs32(re);
    uint32_t ai = iabs32(im);
    uint32_t peak = ar > ai ? ar : ai;
    int sh;
    uint32_t mag;
    int left;

    if (peak == 0u) {
        *mant = 0u;
        *exp = 0;
        return;
    }
    /* Bring the larger part into [2^14, 2^15) so the squares fit in 30 bits. */
    sh = 17 - c2fx_clz32(peak);
    if (sh > 0) {
        ar >>= sh;
        ai >>= sh;
    } else if (sh < 0) {
        ar <<= -sh;
        ai <<= -sh;
    }
    mag = ar * ar + ai * ai;
    left = c2fx_clz32(mag) - 1;
    *mant = mag << left;
    *exp = 2 * sh - left;
}

/* Both inputs normalized (or zero). Sum is normalized on output. */
static void add_norm(uint32_t *m0, int *e0, uint32_t m1, int e1)
{
    uint32_t a;
    uint32_t b;
    int ea;
    int eb;
    int d;
    uint64_t sum;
    int top;
    int sh;

    if (m1 == 0u) {
        return;
    }
    if (*m0 == 0u) {
        *m0 = m1;
        *e0 = e1;
        return;
    }
    a = *m0;
    ea = *e0;
    b = m1;
    eb = e1;
    if (eb > ea) {
        uint32_t tm = a;
        int te = ea;

        a = b;
        ea = eb;
        b = tm;
        eb = te;
    }
    d = ea - eb;
    if (d >= 32) {
        *m0 = a;
        *e0 = ea;
        return;
    }
    sum = (uint64_t)a + ((uint64_t)b >> d);
    top = 63 - c2fx_clz64(sum);
    sh = top - 30;
    if (sh > 0) {
        sum >>= sh;
    } else if (sh < 0) {
        sum <<= -sh;
    }
    *m0 = (uint32_t)sum;
    *e0 = ea + sh;
}

static int32_t log2_mag(uint32_t mant, int exp)
{
    if (mant == 0u) {
        return -(64 << 11);
    }
    return c2fx_log2_q11(mant) + exp * 2048;
}

/* 2^(delta/2048) as a Q20 fraction, for delta <= 0. 1.0 is 1<<20. */
static uint32_t exp2_neg_q20(int32_t delta)
{
    int32_t ip;
    uint32_t fr;
    uint32_t i;
    uint32_t f;
    uint32_t m;
    int shift;

    if (delta >= 0) {
        return 1u << 20;
    }
    if (delta <= -20 * 2048) {
        return 0u;
    }
    {
        uint32_t mag = (uint32_t)(-delta);
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
    m = (uint32_t)c2fx_exp2_tab[i] +
        (((uint32_t)(c2fx_exp2_tab[i + 1] - c2fx_exp2_tab[i]) * f) >> 5);
    /* m is Q14 of 2^(fr/2048) in [1, 2). Result Q20 = m * 2^(ip + 6). */
    shift = ip + 6;
    if (shift >= 0) {
        return m << shift;
    }
    if (-shift >= 31) {
        return 0u;
    }
    return m >> -shift;
}

/* log2 of (sum 2^(v[k]/2048) + 2^(extra/2048)), in Q11. */
int32_t c2fx_log2_sum(const int32_t *v, int n, int32_t extra)
{
    int32_t maxv = extra;
    uint32_t acc = 0;

    for (int k = 0; k < n; k++) {
        if (v[k] > maxv) {
            maxv = v[k];
        }
    }
    acc += exp2_neg_q20(extra - maxv);
    for (int k = 0; k < n; k++) {
        acc += exp2_neg_q20(v[k] - maxv);
    }
    if (acc == 0u) {
        return maxv - 20 * 2048;
    }
    return maxv + c2fx_log2_q11(acc) - 20 * 2048;
}

/* Multiply (re, im) by cis(2*pi/512), both factors in Q30. */
static void rot_bin(int32_t *re, int32_t *im)
{
    int32_t r = *re;
    int32_t i = *im;

    *re = c2fx_mulshr(r, ROT_COS_Q30, 30) - c2fx_mulshr(i, ROT_SIN_Q30, 30);
    *im = c2fx_mulshr(r, ROT_SIN_Q30, 30) + c2fx_mulshr(i, ROT_COS_Q30, 30);
}

/* Multiply (re, im) by (cr, ci), all Q30. */
static void rot_by(int32_t *re, int32_t *im, int32_t cr, int32_t ci)
{
    int32_t r = *re;
    int32_t i = *im;

    *re = c2fx_mulshr(r, cr, 30) - c2fx_mulshr(i, ci, 30);
    *im = c2fx_mulshr(r, ci, 30) + c2fx_mulshr(i, cr, 30);
}

/* log2(|A|^2) or log2(|A|^2 + 1e-6) at the 256 bins of a 512-point DFT.
 * Not an FFT: the other 501 samples are zero, so each bin is the 11-tap
 * polynomial at that frequency. A Q15 FFT loses the cancellation on sharp
 * frames (the residual is a few thousand counts, the same size as the
 * twiddle error). This rotation is Q30, and coeff[k] / 2^q is the coefficient.
 */
static void dft_log_power(const int32_t coeff[11], int q, int floored, int32_t out[SPECTRUM_BINS])
{
    int32_t step_r = 1 << 30;
    int32_t step_i = 0;

    for (int bin = 0; bin < SPECTRUM_BINS; bin++) {
        int32_t cis_r = 1 << 30;
        int32_t cis_i = 0;
        int32_t acc_r = coeff[0];
        int32_t acc_i = 0;
        uint32_t mant;
        int exp;
        int true_exp;

        for (int k = 1; k <= C2FX_LSP_ORDER; k++) {
            rot_by(&cis_r, &cis_i, step_r, step_i);
            acc_r += c2fx_mulshr(coeff[k], cis_r, 30);
            acc_i += c2fx_mulshr(coeff[k], cis_i, 30);
        }
        mag2_norm(acc_r, acc_i, &mant, &exp);
        /* |A|^2 = |acc|^2 * 2^(-2*q). */
        true_exp = exp - 2 * q;
        if (floored) {
            add_norm(&mant, &true_exp, C2FX_POWER_FLOOR_MANT, C2FX_POWER_FLOOR_EXP);
        }
        out[bin] = log2_mag(mant, true_exp);
        rot_bin(&step_r, &step_i);
    }
}

/* trunc((m ± 0.5) * w0 / r + 0.5) on the exact rational.
 * Grid: w0 scale = (128 + 7*index) / 40, and numer = 2*(m ± 0.5),
 * so the edge is (numer * (128 + 7*index) + 40) / 80.
 * Midpoint of two indices uses their sum: scale (256 + 7*sum) / 80,
 * edge (numer * (256 + 7*sum) + 80) / 160.
 * An exact integer stays that integer.
 */
static int band_edge(int numer, int key, int midpoint)
{
    int num = midpoint ? 256 + 7 * key : 128 + 7 * key;
    int div = midpoint ? 160 : 80;
    int edge = (numer * num + div / 2) / div;

    if (edge < 0) {
        return 0;
    }
    if (edge > SPECTRUM_BINS) {
        return SPECTRUM_BINS;
    }
    return edge;
}

static int harmonic_count(int key, int midpoint)
{
    if (!midpoint) {
        return 10240 / (128 + 7 * key);
    }
    return 20480 / (256 + 7 * key);
}

static int harm1_scaled(int key, int midpoint)
{
    /* Strictly below 150 Hz. Index 36 is 148.4 Hz, 37 is 151.2 Hz.
     * A midpoint sum of 73 is still under 150 Hz; 74 is over.
     */
    if (!midpoint) {
        return key <= 36;
    }
    return key <= 73;
}

static int16_t sat_q11(int32_t x)
{
    if (x > 32767) {
        return 32767;
    }
    if (x < -32768) {
        return -32768;
    }
    return (int16_t)x;
}

static int32_t half_q11(int32_t x)
{
    if (x >= 0) {
        return (x + 1) / 2;
    }
    return -((-x + 1) / 2);
}

void c2fx_envelope(
    c2fx_subframe *sub,
    int32_t log_den[SPECTRUM_BINS],
    int32_t log_num[SPECTRUM_BINS],
    int32_t log_p[SPECTRUM_BINS])
{
    int32_t wide[C2FX_LSP_ORDER + 1];
    int32_t gamma[C2FX_LSP_ORDER + 1];
    int q;
    int32_t log_scale;
    int count;
    int scale_h1;

    for (int m = 0; m < C2FX_MAX_HARMONICS; m++) {
        sub->amp_log2_q11[m] = 0;
    }
    /* log_den is not live until the DFT below. The LSP step borrows its
     * first 39 words for P, Q and the quadratic product.
     */
    c2fx_lsp_to_lpc(sub->lsp_q1, wide, &q, log_den);
    gamma[0] = wide[0];
    for (int k = 1; k <= C2FX_LSP_ORDER; k++) {
        gamma[k] = c2fx_mulshr(wide[k], gamma_q15[k], 15);
    }

    dft_log_power(wide, q, 1, log_den);
    dft_log_power(gamma, q, 0, log_num);

    for (int bin = 0; bin < SPECTRUM_BINS; bin++) {
        int32_t diff = log_num[bin] - log_den[bin];
        int32_t beta_term = c2fx_mulshr(diff, C2FX_BETA_Q15, 15);

        /* log2(P') = -log2(|A|^2+eps) + beta * (log2(|Ag|^2) - log2(|A|^2+eps)). */
        log_p[bin] = -log_den[bin] + beta_term;
    }

    /* log2(P) = -log2(|A|^2 + eps). Rescale so sum(P') matches sum(P),
     * with 1e-4 added to each sum.
     */
    for (int bin = 0; bin < SPECTRUM_BINS; bin++) {
        log_den[bin] = -log_den[bin];
    }
    log_scale = c2fx_log2_sum(log_den, SPECTRUM_BINS, C2FX_SUM_EPS_LOG2_Q11) -
                c2fx_log2_sum(log_p, SPECTRUM_BINS, C2FX_SUM_EPS_LOG2_Q11);
    for (int bin = 0; bin < SPECTRUM_BINS; bin++) {
        log_p[bin] += log_scale;
        log_p[bin] += sub->energy_log2_q11;
    }
    for (int bin = 0; bin < 64; bin++) {
        log_p[bin] += C2FX_BASS_LOG2_Q11;
    }

    count = harmonic_count(sub->pitch_key, sub->pitch_mid);
    if (count > C2FX_MAX_HARMONICS) {
        count = C2FX_MAX_HARMONICS;
    }
    sub->count = count;
    scale_h1 = harm1_scaled(sub->pitch_key, sub->pitch_mid);
    for (int m = 1; m <= count; m++) {
        int lo = band_edge(2 * m - 1, sub->pitch_key, sub->pitch_mid);
        int hi = band_edge(2 * m + 1, sub->pitch_key, sub->pitch_mid);
        int32_t log_amp;

        if (hi <= lo) {
            sub->amp_log2_q11[m - 1] = -32768;
            continue;
        }
        log_amp = half_q11(c2fx_log2_sum(log_p + lo, hi - lo, -(64 << 11)));
        if (m == 1 && scale_h1) {
            log_amp += C2FX_HARM1_LOG2_Q11;
        }
        sub->amp_log2_q11[m - 1] = sat_q11(log_amp);
    }
}

void c2fx_lpc_log_inv_power(const int16_t lsp_q1[C2FX_LSP_ORDER], int32_t log2_q11[256])
{
    int32_t wide[C2FX_LSP_ORDER + 1];
    int q;

    /* log2_q11 is filled by the DFT below. The LSP step borrows its first
     * 39 words for P, Q and the quadratic product.
     */
    c2fx_lsp_to_lpc(lsp_q1, wide, &q, log2_q11);
    /* Floored |A|^2, then negate: log2 of the model's lpc power. */
    dft_log_power(wide, q, 1, log2_q11);
    for (int bin = 0; bin < SPECTRUM_BINS; bin++) {
        log2_q11[bin] = -log2_q11[bin];
    }
}

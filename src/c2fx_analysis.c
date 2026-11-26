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
#define LSP_GRID 128
#define LSP_BISECT 6
#define GRID_ONE (INT32_C(1) << 30)
#define GRID_MARGIN 4374818 /* 1e-4 radians as a Q30 fraction of a grid cell */
#define GRID_STEP_RAD 26353589 /* pi / 128 in Q30 */
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
        s->work.lpc.r[0] = 0;
        return 0;
    }
    /* The top bit goes to bit 14. INT16_MIN already fills the range. */
    boost = peak == 32768u ? 0 : c2fx_clz32(peak) - 17;
    gain = 1 << boost;

    c2fx_hann_q38(s->work.lpc.windowed);
    s->work.lpc.windowed[C2FX_ANALYSIS_LEN - 1] = 0;
    for (int i = 1; i < C2FX_ANALYSIS_CENTER; i++) {
        int mirror = C2FX_ANALYSIS_LEN - 1 - i;
        int32_t tap = s->work.lpc.windowed[i];

        s->work.lpc.windowed[i] = (int32_t)c2fx_mulshr_round(x[i] * gain, tap, 38 - WINDOW_Q);
        s->work.lpc.windowed[mirror] = (int32_t)c2fx_mulshr_round(x[mirror] * gain, tap, 38 - WINDOW_Q);
    }
    s->work.lpc.windowed[C2FX_ANALYSIS_CENTER] = (int32_t)c2fx_mulshr_round(
        x[C2FX_ANALYSIS_CENTER] * gain, s->work.lpc.windowed[C2FX_ANALYSIS_CENTER], 38 - WINDOW_Q);

    for (int k = 0; k <= C2FX_LSP_ORDER; k++) {
        int64_t sum = 0;
        const int32_t *left = s->work.lpc.windowed;
        const int32_t *right = left + k;
        const int32_t *end = left + C2FX_ANALYSIS_LEN - k;
        const int32_t *block_end = left + ((C2FX_ANALYSIS_LEN - k) & ~3);

        /* Amortize the loop branch over four MACs on Cortex-M4. */
        for (; left != block_end; left += 4, right += 4) {
            sum = c2fx_mac32(sum, left[0], right[0]);
            sum = c2fx_mac32(sum, left[1], right[1]);
            sum = c2fx_mac32(sum, left[2], right[2]);
            sum = c2fx_mac32(sum, left[3], right[3]);
        }
        for (; left != end; left++, right++) {
            sum = c2fx_mac32(sum, *left, *right);
        }
        s->work.lpc.r[k] = sum;
    }
    /* The boosted peak and every interior Hann tap are large enough
     * that r[0] is nonzero. Put its high bit at CORR_BIT to leave headroom
     * for the LPC sum. Each lag is the stored value times 2^(shift-40-2*boost).
     */
    int shift = 63 - c2fx_clz64((uint64_t)s->work.lpc.r[0]) - CORR_BIT;
    for (int k = 0; k <= C2FX_LSP_ORDER; k++) {
        if (shift > 0) {
            s->work.lpc.r[k] >>= shift;
        } else {
            s->work.lpc.r[k] *= INT64_C(1) << -shift;
        }
    }
    /* White-noise correction, 2e-7 in Q52. */
    s->work.lpc.r[0] = c2fx_add64(s->work.lpc.r[0], c2fx_mulshr64(s->work.lpc.r[0], WHITE_NOISE, LPC_Q));
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

uint64_t c2fx_levinson_q52(const int64_t *r, int64_t *a, int order)
{
    int64_t e = r[0];

    a[0] = LPC_ONE;
    /* A reflection magnitude <= 1 bounds |a[j]| by binomial(i, j),
     * hence at most 252. The lag sum uses order <= 9, whose coefficient
     * magnitudes sum to at most 512 (apart from fixed-point rounding).
     */
    for (int i = 1; i <= order && e > 0; i++) {
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
        int64_t difference = c2fx_add64(mirror, -prev);

        prev = c2fx_add64(direct, sign > 0 ? difference : -difference);
        poly[LSP_HALF_ORDER - k] = k < LSP_HALF_ORDER ? prev * 2 : prev;
    }
}

/* round(2^30 * cos(pi * i / 128)), i = 0..128. The first
 * quadrant also supplies the sine slopes for interpolation.
 */
static const int32_t grid_cos_q30[LSP_GRID / 2 + 1] = {
    1073741824, 1073418433, 1072448455, 1070832474, 1068571464, 1065666786,
    1062120190, 1057933813, 1053110176, 1047652185, 1041563127, 1034846671,
    1027506862, 1019548121, 1010975242, 1001793390, 992008094, 981625251,
    970651112, 959092290, 946955747, 934248793, 920979082, 907154608,
    892783698, 877875009, 862437520, 846480531, 830013654, 813046808,
    795590213, 777654384, 759250125, 740388522, 721080937, 701339000,
    681174602, 660599890, 639627258, 618269338, 596538995, 574449320,
    552013618, 529245404, 506158392, 482766489, 459083786, 435124548,
    410903207, 386434353, 361732726, 336813204, 311690799, 286380643,
    260897982, 235258165, 209476638, 183568930, 157550647, 131437462,
    105245103, 78989349, 52686014, 26350943, 0,
};

static int32_t grid_cos(int i)
{
    return i <= LSP_GRID / 2 ? grid_cos_q30[i] : -grid_cos_q30[LSP_GRID - i];
}

static int32_t grid_slope(int i)
{
    int sine = i < LSP_GRID / 2 ? LSP_GRID / 2 - i : i - LSP_GRID / 2;

    return c2fx_mulshr(-grid_cos_q30[sine], GRID_STEP_RAD, 30);
}

/* Clenshaw's intermediate at k is a sum of poly[j] U_(j-k)(x),
 * with |U_n(x)| <= n+1. Normalizing sum(j * |poly[j]|) + |poly[0]|
 * below 2^29 gives headroom for 2*x*curr, additions and rounding.
 */
static void normalize_polynomial(const int64_t *wide, int32_t *poly)
{
    int64_t bound = (int64_t)c2fx_abs64(wide[0]);
    int64_t tail = 0;

    for (int k = LSP_HALF_ORDER; k > 0; k--) {
        tail = c2fx_add64(tail, (int64_t)c2fx_abs64(wide[k]));
        bound = c2fx_add64(bound, tail);
    }
    /* The leading coefficient is always nonzero. */
    int shift = 64 - c2fx_clz64((uint64_t)bound) - 29;

    for (int k = 0; k <= LSP_HALF_ORDER; k++) {
        poly[k] = (int32_t)(wide[k] >> shift);
    }
}

static int32_t cosine_poly(const int32_t *poly, int32_t x)
{
    int32_t b5 = poly[5];
    int32_t b4 = poly[4] + c2fx_mulshr(b5, x, 29);
    int32_t b3 = poly[3] + c2fx_mulshr(b4, x, 29) - b5;
    int32_t b2 = poly[2] + c2fx_mulshr(b3, x, 29) - b4;
    int32_t b1 = poly[1] + c2fx_mulshr(b2, x, 29) - b3;

    return poly[0] + c2fx_mulshr(b1, x, 30) - b2;
}

/* Cubic Hermite interpolation of cosine within one cell. Its analytic
 * error is at most (pi / 128)^4 / 384 before fixed-point rounding.
 */
typedef struct cosine_cell {
    int32_t x;
    int32_t slope;
    int32_t quadratic;
    int32_t cubic;
} cosine_cell;

static cosine_cell make_cell(int i)
{
    int32_t left = grid_cos(i);
    int32_t delta = grid_cos(i + 1) - left;
    int32_t d0 = grid_slope(i);
    int32_t d1 = grid_slope(i + 1);

    return (cosine_cell){left, d0, 3 * delta - 2 * d0 - d1, d0 + d1 - 2 * delta};
}

static int32_t cell_cos(const cosine_cell *cell, int32_t t)
{
    int32_t n = cell->quadratic + c2fx_mulshr(cell->cubic, t, 30);

    n = cell->slope + c2fx_mulshr(n, t, 30);
    return cell->x + c2fx_mulshr(n, t, 30);
}

static int brackets_root(int32_t left, int32_t right)
{
    return left == 0 || right == 0 || ((left < 0) != (right < 0));
}

static int32_t refine_root(const int32_t *poly, const cosine_cell *cell,
                           int32_t lo, int32_t hi, int32_t left, int32_t right)
{
    for (int step = 0; step < LSP_BISECT && left != 0 && right != 0; step++) {
        int32_t mid = lo + (hi - lo) / 2;
        int32_t value = cosine_poly(poly, cell_cos(cell, mid));

        if (brackets_root(left, value)) {
            hi = mid;
            right = value;
        } else {
            lo = mid;
            left = value;
        }
    }
    if (left == 0) {
        return lo;
    }
    if (right == 0) {
        return hi;
    }

    /* Interpolate the final sign change. Scale magnitudes to at most
     * 16 bits so the Q16 numerator fits in uint32_t.
     */
    uint32_t mag = (uint32_t)(left < 0 ? -left : left);
    uint32_t span = mag + (uint32_t)(right < 0 ? -right : right);
    int bits = 32 - c2fx_clz32(span);
    int shift = bits > 16 ? bits - 16 : 0;
    int32_t fraction = (int32_t)(((mag >> shift) << 16) / (span >> shift));

    return lo + c2fx_mulshr(hi - lo, fraction, 16);
}

/* The symmetric and antisymmetric roots of the expanded stable LPC
 * interlace, starting with P. After each root, search the rest of its
 * cell for the next one before advancing the grid.
 */
static int find_roots(const int32_t *p, const int32_t *q, int32_t *hz)
{
    cosine_cell first = make_cell(0);
    const int32_t *poly = p;
    int32_t left = cosine_poly(poly, cell_cos(&first, GRID_MARGIN));
    int count = 0;

    for (int i = 0; i < LSP_GRID; i++) {
        int32_t lo = i == 0 ? GRID_MARGIN : 0;
        int32_t hi = GRID_ONE;
        int32_t x = grid_cos(i + 1);

        if (i == LSP_GRID - 1) {
            cosine_cell last = make_cell(i);

            hi -= GRID_MARGIN;
            x = cell_cos(&last, hi);
        }
        int32_t right = cosine_poly(poly, x);

        if (!brackets_root(left, right)) {
            left = right;
            continue;
        }
        do {
            cosine_cell cell = make_cell(i);
            int32_t t = refine_root(poly, &cell, lo, hi, left, right);

            /* Each cell spans 4000 / 128 Hz, or 2,048,000 in Q16 Hz.
             * Multiply the Q30 fraction by that span and round.
             */
            hz[count] = i * 2048000 + (int32_t)((c2fx_umul32((uint32_t)t, 125u) + 32768u) >> 16);
            if (count > 0 && hz[count] <= hz[count - 1]) {
                return 0;
            }
            if (++count == C2FX_LSP_ORDER) {
                return 1;
            }
            poly = count % 2 == 0 ? p : q;
            lo = t;
            left = cosine_poly(poly, cell_cos(&cell, lo));
            right = cosine_poly(poly, x);
        } while (brackets_root(left, right));
        left = right;
    }
    return 0;
}

static void lpc_to_lsp(c2fx_enc_row *row, struct c2fx_enc_scratch *s)
{
    int32_t p[LSP_HALF_ORDER + 1];
    int32_t q[LSP_HALF_ORDER + 1];

    for (int k = 1; k <= C2FX_LSP_ORDER; k++) {
        s->work.lpc.a[k] = c2fx_mulshr64(s->work.lpc.a[k], bandwidth_q30[k - 1], 30);
    }

    split_polynomial(s->work.lpc.a, 1, s->work.lpc.poly);
    normalize_polynomial(s->work.lpc.poly, p);
    split_polynomial(s->work.lpc.a, -1, s->work.lpc.poly);
    normalize_polynomial(s->work.lpc.poly, q);
    if (!find_roots(p, q, row->lsp_hz_q16)) {
        flat_lsps(row, 1, C2FX_LSP_ORDER + 1);
    }
}

void c2fx_lpc_analyze(const int16_t x[C2FX_ANALYSIS_LEN],
                      c2fx_enc_row *row, struct c2fx_enc_scratch *s)
{
    int exponent = autocorrelation(x, s);
    uint64_t energy;

    if (s->work.lpc.r[0] == 0) {
        row->energy_log2_q24 = INT32_MIN;
        flat_lsps(row, 0, C2FX_LSP_ORDER);
        return;
    }
    for (int j = 1; j <= C2FX_LSP_ORDER; j++) {
        s->work.lpc.a[j] = 0;
    }
    energy = c2fx_levinson_q52(s->work.lpc.r, s->work.lpc.a, C2FX_LSP_ORDER);
    row->energy_log2_q24 = energy == 0 ? INT32_MIN : energy_log2(energy, exponent);
    lpc_to_lsp(row, s);
}

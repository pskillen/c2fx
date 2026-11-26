// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The c2fx Authors

#include "c2fx_internal.h"

#include "c2fx_window.h"
#include "dsp/c2fx_fxp.h"

#define PITCH_BLOCK 320
#define DECIMATION 5
#define DECIMATED (PITCH_BLOCK / DECIMATION)
#define LP_TAPS 48
#define BIN_MIN 16
#define BIN_MAX 128
#define SUBHARM_BINS 2
#define CORR_HALF 100
#define HZ_ONE (INT32_C(1) << 20)
#define NOTCH_POLE 1020054733 /* round(0.95 * 2^30) */

/* Symmetric, unit-DC 600 Hz windowed sinc with a Hamming window, in Q17. */
static const int16_t lowpass[24] = {
    -142, -144, -122, -55, 72, 263, 486, 674,
    733, 564, 105, -632, -1534, -2385, -2892, -2742,
    -1679, 422, 3498, 7277, 11312, 15047, 17923, 19487,
};

/* First half of the symmetric 64-point Hann in Q16. */
static const uint16_t decimated_hann[32] = {
    0, 163, 650, 1456, 2573, 3990, 5694, 7666,
    9888, 12337, 14990, 17819, 20797, 23893, 27078, 30319,
    33585, 36842, 40060, 43204, 46245, 49152, 51896, 54450,
    56789, 58888, 60729, 62291, 63560, 64523, 65170, 65495,
};

/* The second-order oscillator uses y[k+1] = 2 cos(w) y[k] - y[k-1].
 * Keep y and its difference in Q30, with 2 - 2 cos(w) in Q37.
 * Return the current coefficient in Q20 before advancing.
 */
static int32_t oscillator(int32_t *sample, int32_t *delta, int32_t pole)
{
    int32_t y = *sample;

    *delta -= (int32_t)(((int64_t)y * pole + (INT64_C(1) << 36)) >> 37);
    *sample = y + *delta;
    return (y + 512) >> 10;
}

struct pitch_complex {
    int32_t re;
    int32_t im;
};

static inline struct pitch_complex rotate(struct pitch_complex z, int32_t c, int32_t s)
{
    int64_t sum = (int64_t)(z.re + z.im) * c;
    struct pitch_complex r = {
        (int32_t)((sum - (int64_t)z.im * (c + s) + (1 << 19)) >> 20),
        (int32_t)((sum + (int64_t)z.re * (s - c) + (1 << 19)) >> 20)
    };

    return r;
}

static inline struct pitch_complex load_pair(const int32_t *p)
{
    struct pitch_complex z = {p[0], p[1]};

    return z;
}

/* In-place four-point DFT on four strided complex pairs. */
static void four_point(int32_t *a, int stride)
{
    int32_t *b = a + stride;
    int32_t *c = b + stride;
    int32_t *d = c + stride;
    int32_t er = a[0] + c[0];
    int32_t ei = a[1] + c[1];
    int32_t or = b[0] + d[0];
    int32_t oi = b[1] + d[1];
    int32_t dr = a[0] - c[0];
    int32_t di = a[1] - c[1];
    int32_t ur = b[0] - d[0];
    int32_t ui = b[1] - d[1];

    a[0] = er + or;
    a[1] = ei + oi;
    b[0] = dr + ui;
    b[1] = di - ur;
    c[0] = er - or;
    c[1] = ei - oi;
    d[0] = dr - ui;
    d[1] = di + ur;
}

static void fft_sines(int32_t sine[129])
{
    int32_t y = 0;
    int32_t delta = 13176464; /* sin(2 pi / 512) in Q30 */

    for (unsigned i = 0; i <= 128; i++) {
        sine[i] = oscillator(&y, &delta, 20697801);
    }
}

static void finish_cfft(int32_t *fft, const int32_t *sine)
{
    for (int width = 8; width <= 128; width *= 4) {
        unsigned step = 128u / (unsigned)width;

        for (int k = 0; k < width; k += 2) {
            struct pitch_complex twiddle[4];

            for (unsigned t = 1; k != 0 && t < 4; t++) {
                unsigned phase = t * (unsigned)k * step;

                int32_t c = sine[128 - (phase & 127u)];
                int32_t ns = -sine[phase & 127u];

                if ((phase & 128u) != 0) {
                    int32_t real = ns;

                    ns = -c;
                    c = real;
                }
                if ((phase & 256u) != 0) {
                    c = -c;
                    ns = -ns;
                }
                twiddle[t].re = c;
                twiddle[t].im = ns;
            }
            for (int j = k; j < 512; j += 4 * width) {
                int32_t *p = fft + j;

                for (int t = 1; k != 0 && t < 4; t++) {
                    int32_t *q = p + t * width;

                    if (width != 8 || (q[0] | q[1]) != 0) {
                        struct pitch_complex z = rotate(load_pair(q), twiddle[t].re, twiddle[t].im);

                        q[0] = z.re;
                        q[1] = z.im;
                    }
                }
                four_point(p, width);
            }
        }
    }
}

static void finish_rfft(int32_t *fft, int first, const int32_t *sine)
{
    int32_t dc;

    finish_cfft(fft, sine);
    dc = fft[0];
    fft[0] = dc + fft[1];
    fft[1] = dc - fft[1];
    for (int k = first; k <= 128; k++) {
        int32_t *p = fft + 2 * k;
        int32_t *q = fft + 512 - 2 * k;
        int32_t sr = (p[0] + q[0]) >> 1;
        int32_t si = (p[1] - q[1]) >> 1;
        struct pitch_complex d = {(p[1] + q[1]) >> 1, (q[0] - p[0]) >> 1};
        struct pitch_complex t = rotate(d, sine[128 - k], -sine[k]);

        p[0] = sr + t.re;
        p[1] = si + t.im;
        if (p != q) {
            q[0] = sr - t.re;
            q[1] = t.im - si;
        }
    }
}

static void seed_sparse_cfft(const int32_t input[70], int32_t *fft)
{
    /* The first radix-4 stage follows base-4 digit reversal. Each group
     * has at most one nonzero complex input, so its four outputs are
     * copies. Seed those outputs directly without processing zeros.
     */
    for (unsigned j = 0; j < 64; j++) {
        unsigned i = 2 * ((j & 3u) * 16 + (j & 12u) + j / 16);
        int32_t re = i < 70 ? input[i] : 0;
        int32_t im = i < 70 ? input[i + 1] : 0;

        for (unsigned k = 0; k < 8; k += 2) {
            fft[8 * j + k] = re;
            fft[8 * j + k + 1] = im;
        }
    }
}

/* The input has at most 60 bits. Bring down two bits at a time;
 * the residual fits in 32 bits even before subtracting the trial.
 */
static uint32_t isqrt(uint64_t n)
{
    uint32_t root = 0;
    uint32_t rem = 0;

    n <<= 4;
    for (int i = 0; i < 30; i++) {
        uint32_t trial = (root << 2) + 1;

        rem = (rem << 2) | (uint32_t)(n >> 62);
        root <<= 1;
        if (rem >= trial) {
            rem -= trial;
            root++;
        }
        n <<= 2;
    }
    return root;
}

static uint32_t abs32(int32_t x)
{
    return x < 0 ? 0u - (uint32_t)x : (uint32_t)x;
}

static void coarse_spectrum(const int16_t *x, struct c2fx_enc_scratch *s)
{
    int32_t *notch = s->work.coarse.notch + LP_TAPS - 1;
    int32_t *fft = s->work.coarse.transform.fft;
    int64_t *fir = s->work.coarse.stage.fir;
    uint64_t fir_peak = 0;
    uint32_t peak = 0;
    int32_t prev_sq = 0;
    int32_t prev_y = 0;
    int gain_shift;
    int shift;

    for (int i = 0; i < PITCH_BLOCK; i++) {
        peak |= abs32(x[i]);
    }
    /* Scale squares to at most 2^30, then reserve one bit for the
     * symmetric FIR pairs. The notch subtracts a weighted mean of past
     * squares from the current square.
     */
    gain_shift = peak == 0 || peak >= 32768 ? 0 : 2 * (c2fx_clz32(peak) - 17);
    for (int i = 1 - LP_TAPS; i < 0; i++) {
        notch[i] = 0;
    }
    for (int i = 0; i < PITCH_BLOCK; i++) {
        int32_t sq = (x[i] * x[i] << gain_shift) >> 1;

        prev_y = sq - prev_sq + (int32_t)c2fx_mulshr_round(prev_y, NOTCH_POLE, 30);
        notch[i] = prev_y;
        prev_sq = sq;
    }

    for (int j = 1; j < DECIMATED - 1; j++) {
        int i = j * DECIMATION;
        int64_t sum = 0;

        for (int k = 0; k < LP_TAPS / 2; k++) {
            int32_t pair = notch[i - k] + notch[i - (LP_TAPS - 1 - k)];

            sum = c2fx_mac32(sum, pair, lowpass[k]);
        }
        fir[j] = sum;
        fir_peak |= (uint64_t)(sum ^ (sum >> 63));
    }
    /* The FFT has only 64 nonzero inputs. Limit each input to 24
     * magnitude bits to leave room for the unpacking pair sums.
     */
    shift = fir_peak == 0 ? 0 : 40 - c2fx_clz64(fir_peak);
    notch[0] = 0;
    for (int j = 1; j < DECIMATED - 1; j++) {
        int32_t sample = (int32_t)(shift >= 0 ? (uint64_t)(fir[j] >> shift) : (uint64_t)fir[j] << -shift);
        int k = j < DECIMATED / 2 ? j : DECIMATED - 1 - j;

        notch[j] = c2fx_mulshr(sample, decimated_hann[k], 16);
    }
    for (int j = DECIMATED - 1; j < 70; j++) {
        notch[j] = 0;
    }
    fft_sines(notch + 70);
    seed_sparse_cfft(notch, fft);
    finish_rfft(fft, BIN_MIN - 1, notch + 70);
    peak = 0;
    for (int i = 2 * (BIN_MIN - 1); i < 2 * (BIN_MAX + 2); i++) {
        peak |= abs32(fft[i]);
    }
    /* Normalize to at most 31 power bits, with a common exponent. */
    shift = peak == 0 ? 0 : 2 * (17 - c2fx_clz32(peak));
    if (shift < 0) {
        shift = 0;
    }
    for (int k = BIN_MIN - 1; k <= BIN_MAX + 1; k++) {
        int32_t re = fft[2 * k];
        int32_t im = fft[2 * k + 1];

        s->work.coarse.stage.power[k] = (uint32_t)(((int64_t)re * re + (int64_t)im * im) >> shift);
    }
}

static int local_peak(const uint32_t *power, int k)
{
    return power[k] >= power[k - 1] && power[k] >= power[k + 1];
}

/* Ratios compare squared magnitudes in the same power scale. */
static int ratio_pass(uint32_t cand, uint32_t peak, int32_t ratio)
{
    return ((uint64_t)cand << 28) >= c2fx_umul32(peak, (uint32_t)(ratio * ratio));
}

/* Shorten a unit fraction to a native 32-bit division. */
static int32_t unit_fraction(int32_t num, int32_t den, unsigned bits)
{
    uint32_t n = abs32(num);
    uint32_t d = abs32(den);
    uint32_t q = 1u << bits;

    if (n < d) {
        unsigned shift = (unsigned)c2fx_clz32(d);

        q = (n << shift) / ((d << shift) >> bits);
        if (q > (1u << bits)) {
            q = 1u << bits;
        }
    }
    return (num < 0) != (den < 0) ? -(int32_t)q : (int32_t)q;
}

/* The reference window is common to every lag. Keep its energy at
 * lag zero and roll the same 200-sample window backward once per row.
 */
static void period_energies(const int16_t *x, int64_t *energy)
{
    int64_t sum = 0;

    for (int j = 0; j < 2 * CORR_HALF; j++) {
        sum = c2fx_mac32(sum, x[j], x[j]);
    }
    energy[0] = sum;
    for (int lag = 1; lag <= C2FX_PITCH_MAX_LAG; lag++) {
        int32_t enter = x[-lag];
        int32_t leave = x[2 * CORR_HALF - lag];

        sum = c2fx_mac32(sum, enter, enter);
        sum = c2fx_mac32(sum, -leave, leave);
        energy[lag] = sum;
    }
}

static int32_t period_corr(const int16_t *x, int bin, const int64_t *energy_by_lag)
{
    int lag = (5120 + bin) / (2 * bin); /* round(2560 / bin) */
    int64_t ab = 0;
    int64_t aa = energy_by_lag[0];
    int64_t bb = energy_by_lag[lag];
    uint64_t largest;
    uint32_t energy;
    int shift;

    for (int j = 0; j < 2 * CORR_HALF; j++) {
        int32_t u = x[j];
        int32_t v = x[j - lag];

        ab = c2fx_mac32(ab, u, v);
    }
    largest = (uint64_t)(aa | bb) | 1u;
    shift = 34 - c2fx_clz64(largest);
    if (shift < 0) {
        shift = 0;
    }
    energy = isqrt(c2fx_umul32((uint32_t)(aa >> shift), (uint32_t)(bb >> shift)));
    return energy == 0 ? 0 : unit_fraction((int32_t)(ab >> shift), (int32_t)energy, 10);
}

static int32_t candidate_ratio(int bin, int32_t prev)
{
    uint32_t hz = (uint32_t)bin * (25 * (HZ_ONE / 8));
    int32_t ratio = 9011; /* 0.55 in Q14 */

    if (bin < 32) {
        ratio = (7717 * 16 + 1311 * (bin - BIN_MIN) + 8) / 16;
    }
    if (5 * hz > 4u * (uint32_t)prev && 5 * hz < 6u * (uint32_t)prev) {
        ratio = 6390; /* 0.39 */
    } else if (8 * hz < 5u * (uint32_t)prev) {
        ratio = 12943; /* 0.79 */
    }
    return ratio;
}

int32_t c2fx_pitch_coarse(const int16_t x[C2FX_PITCH_SPAN], int32_t prev_hz_q20,
                          struct c2fx_enc_scratch *s)
{
    const uint32_t *power = s->work.coarse.stage.power;
    const int16_t *corr = x + C2FX_PITCH_BEFORE - CORR_HALF;
    int64_t *energy_by_lag = s->work.coarse.transform.energy_by_lag;
    int best = BIN_MIN;
    int chosen;
    int32_t peak_corr = 2048; /* Outside the Q10 correlation range. */
    int32_t delta = 0;

    coarse_spectrum(x + C2FX_PITCH_BEFORE - PITCH_BLOCK / 2, s);
    for (int k = BIN_MIN + 1; k <= BIN_MAX; k++) {
        if (power[k] > power[best]) {
            best = k;
        }
    }
    if (power[best] == 0) {
        return 50 * HZ_ONE;
    }
    chosen = best;
    for (int div = 2; div * BIN_MIN <= best; div++) {
        int lo = best / div - SUBHARM_BINS;
        int hi = (best + div - 1) / div + SUBHARM_BINS;
        int ck;

        if (lo < BIN_MIN) {
            lo = BIN_MIN;
        }
        ck = lo;
        for (int k = lo + 1; k <= hi; k++) {
            if (power[k] > power[ck]) {
                ck = k;
            }
        }
        if (ck < chosen && local_peak(power, ck) &&
            ratio_pass(power[ck], power[best], candidate_ratio(ck, prev_hz_q20))) {
            if (peak_corr == 2048) {
                period_energies(corr, energy_by_lag);
                peak_corr = period_corr(corr, best, energy_by_lag);
            }
            if (period_corr(corr, ck, energy_by_lag) >= peak_corr - 614) { /* -0.6 in Q10 */
                chosen = ck;
            }
        }
    }
    for (int k = 20; k < best * 2 / 3 && k < chosen; k++) { /* ceil(60 / 3.125) */
        if (local_peak(power, k) && ratio_pass(power[k], power[best], 10158)) { /* 0.62 */
            chosen = k;
            break;
        }
    }
    {
        int32_t left = (int32_t)isqrt((uint64_t)power[chosen - 1] << 29);
        int32_t mid = (int32_t)isqrt((uint64_t)power[chosen] << 29);
        int32_t right = (int32_t)isqrt((uint64_t)power[chosen + 1] << 29);
        int32_t den = (left - mid) + (right - mid);
        int32_t num = left - right;

        if (den != 0) {
            delta = unit_fraction(num, den, 19);
        }
    }
    return (int32_t)(((uint32_t)(chosen * HZ_ONE + delta) * 25u) >> 3);
}

#define HS_STEPS 12
#define HS_BIN_MAX (C2FX_PITCH_POWER_BINS - 1)
#define HS_BIN_ONE (UINT32_C(1) << 20)

/* Half-sample interpolation of the centered complex spectrum.
 * Symmetric Q15 taps fit unity gain by least squares over [0, 139/N]
 * cycles per bin, for N = 512 and 1024 respectively.
 */
static const int16_t midpoint_512[] = {20385, -5639, 2307, -901, 294, -70, 9};
static const int16_t midpoint_1024[] = {19717, -4137, 911, -108};

static struct pitch_complex midpoint(const int32_t *left, int stride,
                                     const int16_t *tap, int pairs)
{
    int64_t re = 16384;
    int64_t im = 16384;

    for (int i = 0; i < pairs; i++) {
        const int32_t *a = left - i * stride;
        const int32_t *b = left + (i + 1) * stride;

        re = c2fx_mac32(re, a[0] + b[0], tap[i]);
        im = c2fx_mac32(im, a[1] + b[1], tap[i]);
    }
    {
        struct pitch_complex result = {(int32_t)(re >> 15), (int32_t)(im >> 15)};

        return result;
    }
}

static int32_t window_sample(const int16_t *x, const int32_t *hann, unsigned i, int shift)
{
    if (i >= C2FX_ANALYSIS_LEN) {
        return 0;
    }
    unsigned j = i <= C2FX_ANALYSIS_CENTER ? i : C2FX_ANALYSIS_LEN - 1 - i;

    return (int32_t)(((int64_t)x[i] * hann[j]) >> shift);
}

static void analysis_power(const int16_t *x, struct c2fx_enc_scratch *s)
{
    int32_t *fft = s->work.refinement.spectrum;
    int32_t *power = s->work.refinement.power;
    int32_t *grid = s->work.refinement.interp.grid + 4 * C2FX_PITCH_GRID_GUARD;
    int32_t *hann = s->work.refinement.interp.hann;
    uint64_t sum = 0;
    int shift;

    c2fx_hann_q38(hann);
    for (int i = 0; i < C2FX_ANALYSIS_CENTER; i++) {
        int mirror = C2FX_ANALYSIS_LEN - 1 - i;
        uint32_t pair = abs32(x[i]) + abs32(x[mirror]);

        sum += c2fx_umul32(pair, (uint32_t)hann[i]);
    }
    sum += c2fx_umul32(abs32(x[C2FX_ANALYSIS_CENTER]), (uint32_t)hann[C2FX_ANALYSIS_CENTER]);
    /* As in the coarse path, reserve room for FFT pair sums. Here the
     * bound is on windowed L1, so it also bounds the final components.
     */
    shift = sum == 0 ? 0 : 35 - c2fx_clz64(sum + (sum >> 9));
    if (shift < 0) {
        shift = 0;
    }
    /* Circularly center the real input before the FFT. A centered
     * window DTFT uses the same origin for the subsequent voicing fit.
     */
    for (unsigned j = 0; j < 64; j++) {
        unsigned sample = 2 * ((j & 3u) * 16 + (j & 12u) + j / 16) + C2FX_ANALYSIS_CENTER;
        int32_t *p = fft + 8 * j;

        for (unsigned t = 0; t < 4; t++) {
            unsigned i = (sample + 128 * t) & 511u;

            p[2 * t] = window_sample(x, hann, i, shift);
            p[2 * t + 1] = window_sample(x, hann, (i + 1) & 511u, shift);
        }
        four_point(p, 2);
    }
    fft_sines(hann);
    finish_rfft(fft, 1, hann);
    grid[0] = (fft[0] + 512) >> 10;
    grid[1] = 0;
    for (int k = 1; k <= C2FX_PITCH_GRID_LAST; k++) {
        grid[4 * k] = (fft[2 * k] + 512) >> 10;
        grid[4 * k + 1] = (fft[2 * k + 1] + 512) >> 10;
    }
    for (int k = -C2FX_PITCH_GRID_GUARD; k < 0; k++) {
        grid[4 * k] = grid[-4 * k];
        grid[4 * k + 1] = -grid[-4 * k + 1];
    }
    /* Odd bins of the 1024 grid, including the guard bins needed by
     * the next filter. Its edge of 139/1024 allows a shorter filter.
     */
    for (int k = -2; k <= HS_BIN_MAX / 4 + 1; k++) {
        int32_t *left = grid + 4 * k;

        struct pitch_complex z = midpoint(left, 4, midpoint_512, 7);

        left[2] = z.re;
        left[3] = z.im;
    }
    for (unsigned k = 0; k <= HS_BIN_MAX; k++) {
        const int32_t *left = grid + 2 * (k / 2);
        struct pitch_complex z = load_pair(left);

        if (k & 1) {
            z = midpoint(left, 2, midpoint_1024, 4);
        }
        power[k] = (int32_t)(((int64_t)z.re * z.re + (int64_t)z.im * z.im) >> 8);
    }
}

int32_t c2fx_pitch_hz(const int16_t x[C2FX_PITCH_SPAN], int32_t prev_hz_q20,
                      struct c2fx_enc_scratch *s)
{
    int32_t coarse = c2fx_pitch_coarse(x, prev_hz_q20, s);
    const int32_t *power = s->work.refinement.power;
    int64_t best_energy = -1;
    int32_t best_hz = 0;

    analysis_power(x + C2FX_PITCH_BEFORE - C2FX_ANALYSIS_CENTER, s);
    for (int step = -HS_STEPS; step <= HS_STEPS; step++) {
        int32_t hz = coarse + (int32_t)((uint32_t)coarse / 200 * abs32(step)) * (step < 0 ? -1 : 1);
        uint32_t spacing = (uint32_t)c2fx_mulshr(hz, 274877907, 30); /* 2048/8000 */
        int64_t energy = 0;

        for (uint32_t bin = spacing; bin <= HS_BIN_MAX * HS_BIN_ONE; bin += spacing) {
            uint32_t k = bin >> 20;
            uint32_t frac = bin & (HS_BIN_ONE - 1);
            int32_t left = power[k];
            int32_t right = k < HS_BIN_MAX ? power[k + 1] : left;

            energy += (int64_t)left * HS_BIN_ONE + (int64_t)(right - left) * frac;
        }
        if (energy > best_energy) {
            best_energy = energy;
            best_hz = hz;
        }
    }
    if (best_hz < 50 * HZ_ONE) {
        return 50 * HZ_ONE;
    }
    if (best_hz > 400 * HZ_ONE) {
        return 400 * HZ_ONE;
    }
    return best_hz;
}

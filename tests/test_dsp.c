// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The c2fx Authors
// Host checks for the fixed-point FFT, cosine, log2 and exp2.
// Headroom: each time sample is in -64..64, so a coherent sum of 512 of
// them stays well inside int32. The transform is unscaled, so the inverse
// of the forward result is 512 times the input, within the error below.

#include "c2fx_dsp.h"
#define C2FX_TEST_PORTABLE_CLZ
#include "c2fx_fxp.h"

#include <math.h>
#include <stdio.h>
#include <stdint.h>

#define FFT_LEN 512
#define HEADROOM 64

static uint32_t xorshift32(uint32_t *state)
{
    uint32_t x = *state;

    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static int test_clz(void)
{
    uint32_t state = UINT32_C(0xC2F0C120);

    for (int i = 0; i < 32; i++) {
        uint32_t x = UINT32_C(1) << i;

        if (c2fx_clz32_portable(x) != 31 - i || c2fx_clz32(x) != 31 - i) {
            fprintf(stderr, "clz32 mismatch at 2^%d\n", i);
            return 1;
        }
    }
    for (int i = 0; i < 64; i++) {
        uint64_t x = UINT64_C(1) << i;

        if (c2fx_clz64_portable(x) != 63 - i || c2fx_clz64(x) != 63 - i) {
            fprintf(stderr, "clz64 mismatch at 2^%d\n", i);
            return 1;
        }
    }
    for (int i = 0; i < 10000; i++) {
        uint32_t lo = xorshift32(&state);
        uint32_t hi = xorshift32(&state);
        uint64_t x = ((uint64_t)hi << 32) | lo;

        if (c2fx_clz32_portable(lo) != c2fx_clz32(lo) ||
            c2fx_clz64_portable(x) != c2fx_clz64(x)) {
            fprintf(stderr, "clz mismatch at random probe %d\n", i);
            return 1;
        }
    }
    return 0;
}

/* The split and wide forms of c2fx_mulshr16 must agree on every operand:
 * edge values of x and of the Q15 factor c (|c| <= 32768), every shift
 * 0..16, then random operands.
 */
static int test_mulshr16(void)
{
    static const int32_t xs[] = {
        INT32_MIN, INT32_MIN + 1, -65537, -65536, -65535, -32769, -32768, -1, 0, 1,
        32767, 32768, 65535, 65536, 65537, INT32_MAX - 1, INT32_MAX
    };
    static const int32_t cs[] = {-32768, -32767, -16384, -1, 0, 1, 16384, 32767, 32768};
    uint32_t state = UINT32_C(0xC2F0A116);

    for (unsigned i = 0; i < sizeof xs / sizeof xs[0]; i++) {
        for (unsigned j = 0; j < sizeof cs / sizeof cs[0]; j++) {
            for (int s = 0; s <= 16; s++) {
                if (c2fx_mulshr16_split(xs[i], cs[j], s) != c2fx_mulshr16_wide(xs[i], cs[j], s) ||
                    c2fx_mulshr16(xs[i], cs[j], s) != c2fx_mulshr16_wide(xs[i], cs[j], s)) {
                    fprintf(stderr, "mulshr16 mismatch x=%d c=%d s=%d\n", (int)xs[i], (int)cs[j], s);
                    return 1;
                }
            }
        }
    }
    for (int i = 0; i < 2000000; i++) {
        int32_t x = (int32_t)xorshift32(&state);
        int32_t c = (int32_t)(xorshift32(&state) % 65537u) - 32768;
        int s = (int)(xorshift32(&state) % 17u);

        if (c2fx_mulshr16_split(x, c, s) != c2fx_mulshr16_wide(x, c, s)) {
            fprintf(stderr, "mulshr16 mismatch x=%d c=%d s=%d\n", (int)x, (int)c, s);
            return 1;
        }
    }
    return 0;
}

static int test_fft(void)
{
    uint32_t state = 0xC2F0512u;
    double dft_worst = 0.0;
    int64_t round_worst = 0;

    for (int trial = 0; trial < 4; trial++) {
        int32_t x[FFT_LEN];
        int32_t buf[FFT_LEN];

        for (int n = 0; n < FFT_LEN; n++) {
            x[n] = (int32_t)(xorshift32(&state) % (2u * HEADROOM + 1u)) - HEADROOM;
            buf[n] = x[n];
        }
        c2fx_rfft512(buf);
        for (int bin = 0; bin < FFT_LEN / 2; bin++) {
            double re = 0.0;
            double im = 0.0;
            double fre = (bin == 0) ? (double)buf[0] : (double)buf[2 * bin];
            double fim = (bin == 0) ? 0.0 : (double)buf[2 * bin + 1];
            double err;

            for (int n = 0; n < FFT_LEN; n++) {
                double ang = -2.0 * 3.14159265358979323846 * (double)bin * (double)n / (double)FFT_LEN;

                re += (double)x[n] * cos(ang);
                im += (double)x[n] * sin(ang);
            }
            err = hypot(fre - re, fim - im);
            if (err > dft_worst) {
                dft_worst = err;
            }
        }
        c2fx_irfft512(buf);
        for (int n = 0; n < FFT_LEN; n++) {
            int64_t err = (int64_t)buf[n] - (int64_t)x[n] * FFT_LEN;

            if (err < 0) {
                err = -err;
            }
            if (err > round_worst) {
                round_worst = err;
            }
        }
    }
    printf("FFT headroom +-%d: max DFT error %.1f LSBs, max round trip %lld LSBs (scale %d)\n",
           HEADROOM, dft_worst, (long long)round_worst, FFT_LEN);
    /* Q15 twiddles. A broken butterfly is thousands of LSBs, not tens. */
    if (dft_worst > 96.0 || round_worst > 2500) {
        fprintf(stderr, "FFT error above the headroom bound (DFT %.1f, round trip %lld)\n",
                dft_worst, (long long)round_worst);
        return 1;
    }
    return 0;
}

static int test_cos(void)
{
    double worst = 0.0;

    for (uint32_t i = 0; i < 65536u; i++) {
        uint32_t phase = i << 16;
        /* Q15 with full scale 32767, as in RadioFoundry's mbe_sin_tab. */
        double truth = 32767.0 * cos(2.0 * 3.14159265358979323846 * (double)phase / 4294967296.0);
        double got = (double)c2fx_cos(phase);
        double err = fabs(got - truth);

        if (err > worst) {
            worst = err;
        }
    }
    printf("cos: max error %.3f Q15 LSBs over 2^16 phases\n", worst);
    if (worst > 2.0) {
        fprintf(stderr, "cos error %.3f Q15 LSBs above 2\n", worst);
        return 1;
    }
    return 0;
}

static int test_log_exp(void)
{
    double log_worst = 0.0;
    double exp_worst = 0.0;
    int round_worst = 0;

    for (uint32_t n = 1; n <= 100000u; n += 1u + (n >> 8)) {
        double err = fabs((double)c2fx_log2_q11(n) / 2048.0 - log2((double)n));

        if (err > log_worst) {
            log_worst = err;
        }
    }
    for (int x = 0; x <= 14 * 2048; x += 1) {
        double err = fabs((double)c2fx_exp2_q11(x) - exp2((double)x / 2048.0));

        if (err > exp_worst) {
            exp_worst = err;
        }
    }
    for (uint32_t n = 1; n <= 100000u; n += 3u) {
        int back = c2fx_exp2_q11(c2fx_log2_q11(n));
        int err = back - (int)n;

        if (err < 0) {
            err = -err;
        }
        if (err > round_worst) {
            round_worst = err;
        }
    }
    printf("log2: max error %.3f Q11 LSBs; exp2: max error %.2f LSBs; exp2(log2) max %d\n",
           log_worst * 2048.0, exp_worst, round_worst);
    if (log_worst * 2048.0 > 2.0 || exp_worst > 4.0 || round_worst > 128) {
        fprintf(stderr, "log2/exp2 above bound (log2 %.3f Q11, exp2 %.2f, round %d)\n",
                log_worst * 2048.0, exp_worst, round_worst);
        return 1;
    }
    return 0;
}

static int fail_div(const char *what, uint64_t x, uint32_t got, uint32_t expect)
{
    fprintf(stderr, "%s x=%llu got %u expect %u\n", what, (unsigned long long)x, got, expect);
    return 1;
}

/* Every x = d*k and x = d*k-1 up to xmax. floor(x/d) changes only there. */
static int check_udiv32_bounds(uint32_t d, uint64_t inv, uint32_t xmax)
{
    uint32_t kmax = xmax / d;

    for (uint32_t k = 0; k <= kmax; k++) {
        uint32_t x = k * d;

        if (c2fx_udiv32_inv(x, inv) != k) {
            return fail_div("udiv32", x, c2fx_udiv32_inv(x, inv), k);
        }
        if (k > 0u && c2fx_udiv32_inv(x - 1u, inv) != k - 1u) {
            return fail_div("udiv32", x - 1u, c2fx_udiv32_inv(x - 1u, inv), k - 1u);
        }
    }
    if (c2fx_udiv32_inv(xmax, inv) != xmax / d) {
        return fail_div("udiv32", xmax, c2fx_udiv32_inv(xmax, inv), xmax / d);
    }
    return 0;
}

/* Phase inputs: x = q1 * 2^26 + 125, q1 in 0..32767. */
static int check_udiv64_phase(void)
{
    for (uint32_t q1 = 0; q1 <= 32767u; q1++) {
        uint64_t x = ((uint64_t)q1 << 26) + 125u;
        uint32_t expect = (uint32_t)(x / 250u);
        uint32_t got = c2fx_udiv64_inv(x, C2FX_INV_U64_250);

        if (got != expect) {
            return fail_div("udiv64", x, got, expect);
        }
    }
    return 0;
}

/* Every x = (d<<30)*k and the integer just below, through xmax. */
static int check_p30_bounds(uint32_t d, uint64_t inv, uint64_t xmax)
{
    uint64_t denom = (uint64_t)d << 30;
    uint64_t kmax = xmax / denom;

    for (uint64_t k = 0; k <= kmax; k++) {
        uint64_t x = k * denom;
        uint32_t expect = (uint32_t)k;

        if (c2fx_udiv_p30_inv(x, inv, d) != expect) {
            return fail_div("p30", x, c2fx_udiv_p30_inv(x, inv, d), expect);
        }
        if (k > 0u && c2fx_udiv_p30_inv(x - 1u, inv, d) != expect - 1u) {
            return fail_div("p30", x - 1u, c2fx_udiv_p30_inv(x - 1u, inv, d), expect - 1u);
        }
    }
    {
        uint32_t expect = (uint32_t)(xmax / denom);
        uint32_t got = c2fx_udiv_p30_inv(xmax, inv, d);

        if (got != expect) {
            return fail_div("p30", xmax, got, expect);
        }
    }
    return 0;
}

static int test_reciprocals(void)
{
    const uint32_t pi_q30 = 3373259426u;
    const uint32_t th = (uint32_t)((((uint64_t)1u << 30) * pi_q30) >> 31);
    const uint32_t th2_max = (uint32_t)(((uint64_t)th * th) >> 30);
    const uint64_t p30_max = (uint64_t)th2_max << 30;

    if (th2_max != 2649351757u) {
        fprintf(stderr, "theta^2 max %u, expected 2649351757\n", th2_max);
        return 1;
    }
    if (check_udiv32_bounds(156u, C2FX_INV_U64_156, 0xffffffffu) != 0 ||
        check_udiv64_phase() != 0 ||
        check_p30_bounds(6u, C2FX_INV_U64_P30_6, p30_max) != 0 ||
        check_p30_bounds(20u, C2FX_INV_U64_P30_20, p30_max) != 0 ||
        check_p30_bounds(42u, C2FX_INV_U64_P30_42, p30_max) != 0 ||
        check_p30_bounds(72u, C2FX_INV_U64_P30_72, p30_max) != 0 ||
        check_p30_bounds(110u, C2FX_INV_U64_P30_110, p30_max) != 0) {
        return 1;
    }
    printf("reciprocals: exact through theta^2 %u and q1 0..32767\n", th2_max);
    return 0;
}

int main(void)
{
    int failed = 0;

    failed |= test_clz();
    failed |= test_mulshr16();
    failed |= test_fft();
    failed |= test_cos();
    failed |= test_log_exp();
    failed |= test_reciprocals();
    return failed ? 1 : 0;
}

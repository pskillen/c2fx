// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The c2fx Authors
// Host checks for the fixed-point FFT, cosine, log2 and exp2.
// Headroom: each time sample is in -64..64, so a coherent sum of 512 of
// them stays well inside int32. The transform is unscaled, so the inverse
// of the forward result is 512 times the input, within the error below.

#include "c2fx_dsp.h"

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
        double truth = cos(2.0 * 3.14159265358979323846 * (double)phase / 4294967296.0);
        double got = (double)c2fx_cos(phase) / 32768.0;
        double err = fabs(got - truth);

        if (err > worst) {
            worst = err;
        }
    }
    printf("cos: max error %.3f Q15 LSBs over 2^16 phases\n", worst * 32768.0);
    if (worst * 32768.0 > 2.0) {
        fprintf(stderr, "cos error %.3f Q15 LSBs above 2\n", worst * 32768.0);
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

int main(void)
{
    int failed = 0;

    failed |= test_fft();
    failed |= test_cos();
    failed |= test_log_exp();
    return failed ? 1 : 0;
}

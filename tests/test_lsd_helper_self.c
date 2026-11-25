// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The c2fx Authors

#include "test_lsd_helper.h"

#include "c2m_synth.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>

/* Self-test of c2m_test_mean_lsd. The distance of a signal from itself is 0,
 * and from the same signal at a known gain it is that gain in dB, in every
 * bin, so the mean is the gain in dB. Gains 2 and 4 keep the scaled samples
 * exact in int16. The signal is deterministic noise, so every analysis bin is
 * far above the helper's magnitude floor of 1.
 */

#define SAMPLES (C2M_FRAME_SAMPLES * 20)
#define TOLERANCE_DB 0.01

static int16_t sig[SAMPLES];
static int16_t twice[SAMPLES];
static int16_t quad[SAMPLES];

static int expect_lsd(const char *what, const int16_t *a, const int16_t *b, double want)
{
    int used = 0;
    double got = c2m_test_mean_lsd(a, b, sig, SAMPLES, &used);

    if (used != 20 || fabs(got - want) > TOLERANCE_DB) {
        fprintf(stderr, "lsd helper: %s gave %.4f dB over %d frames, expected %.4f dB\n", what, got, used, want);
        return 1;
    }
    return 0;
}

int main(void)
{
    uint32_t state = 12345u;
    int failed = 0;

    for (int i = 0; i < SAMPLES; i++) {
        int v;

        state = state * 1664525u + 1013904223u;
        v = (int)((state >> 16) % 8001u) - 4000;
        sig[i] = (int16_t)v;
        twice[i] = (int16_t)(2 * v);
        quad[i] = (int16_t)(4 * v);
    }
    failed |= expect_lsd("signal against itself", sig, sig, 0.0);
    failed |= expect_lsd("signal against 2x gain", sig, twice, 20.0 * log10(2.0));
    failed |= expect_lsd("signal against 4x gain", sig, quad, 20.0 * log10(4.0));
    if (!failed) {
        printf("lsd helper self-test passed\n");
    }
    return failed;
}

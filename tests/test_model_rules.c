// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The c2fx Authors

#include "c2m_analysis.h"
#include "c2m_voicing.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

/* The voicing post-rules, on made-up signals. */

#define N 2400

static int16_t pcm[N];

static int test_tilt(void)
{
    int failed = 0;
    double low;
    double nyq;
    double flat;

    for (int i = 0; i < N; i++) {
        pcm[i] = (int16_t)floor(6000.0 * sin(2.0 * C2M_PI * 150.0 * (double)i / C2M_FS_HZ) + 0.5);
    }
    low = c2m_voicing_tilt(pcm, N, 1200);
    for (int i = 0; i < N; i++) {
        pcm[i] = (int16_t)((i % 2) != 0 ? 6000 : -6000);
    }
    nyq = c2m_voicing_tilt(pcm, N, 1200);
    for (int i = 0; i < N; i++) {
        pcm[i] = 5000;
    }
    flat = c2m_voicing_tilt(pcm, N, 1200);
    if (!(low > 0.95)) {
        fprintf(stderr, "tilt of a 150 Hz sine is %.3f, want above 0.95\n", low);
        failed = 1;
    }
    if (!(nyq < -0.95)) {
        fprintf(stderr, "tilt of a Nyquist tone is %.3f, want below -0.95\n", nyq);
        failed = 1;
    }
    if (fabs(flat) > 1e-9) {
        fprintf(stderr, "tilt of a constant is %.3g, want 0\n", flat);
        failed = 1;
    }
    return failed;
}

static int test_rules(void)
{
    int failed = 0;

    /* Rule 1: a high-frequency frame that passes the SNR test is unvoiced. */
    if (c2m_voicing_base(12.0, -0.5) != 0) {
        fprintf(stderr, "rule 1: SNR 12 dB with tilt -0.5 should be unvoiced\n");
        failed = 1;
    }
    if (c2m_voicing_base(12.0, 0.3) != 1) {
        fprintf(stderr, "base: SNR 12 dB with tilt 0.3 should be voiced\n");
        failed = 1;
    }
    /* Rule 2: strong low-frequency tilt is voiced without the SNR. */
    if (c2m_voicing_base(1.0, 0.9) != 1) {
        fprintf(stderr, "rule 2: SNR 1 dB with tilt 0.9 should be voiced\n");
        failed = 1;
    }
    if (c2m_voicing_base(1.0, 0.3) != 0) {
        fprintf(stderr, "base: SNR 1 dB with tilt 0.3 should be unvoiced\n");
        failed = 1;
    }
    /* Rule 3: an unvoiced row between two voiced rows bridges on tilt. */
    if (c2m_voicing_final(1, 0, 1, 0.7) != 1) {
        fprintf(stderr, "rule 3: a gap with tilt 0.7 between voiced rows should be voiced\n");
        failed = 1;
    }
    if (c2m_voicing_final(1, 0, 1, 0.1) != 0) {
        fprintf(stderr, "rule 3: a gap with tilt 0.1 should stay unvoiced\n");
        failed = 1;
    }
    if (c2m_voicing_final(1, 0, 0, 0.7) != 0 || c2m_voicing_final(0, 0, 1, 0.7) != 0) {
        fprintf(stderr, "rule 3: a gap needs a voiced row on both sides\n");
        failed = 1;
    }
    return failed;
}

int main(void)
{
    int failed = 0;

    failed |= test_tilt();
    failed |= test_rules();
    if (failed == 0) {
        printf("rules: ok\n");
    }
    return failed;
}

// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The c2fx Authors

#include "c2m_analysis.h"
#include "c2m_pitch.h"
#include "c2m_voicing.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

/* The voicing post-rules and the pitch tracker, on made-up signals. */

#define N 2400

static int16_t pcm[N];

static void make_harmonic(double f0_hz, int harmonics)
{
    for (int i = 0; i < N; i++) {
        double s = 0.0;

        for (int m = 1; m <= harmonics; m++) {
            s += sin(2.0 * C2M_PI * (double)m * f0_hz * (double)i / C2M_FS_HZ + 0.7 * (double)m) / (double)m;
        }
        pcm[i] = (int16_t)floor(4000.0 * s + 0.5);
    }
}

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

static int test_pitch(void)
{
    static const double rates[] = {80.0, 120.0, 160.0, 220.0, 300.0};
    int failed = 0;

    for (size_t r = 0; r < sizeof rates / sizeof rates[0]; r++) {
        double f0 = rates[r];

        make_harmonic(f0, 20);
        for (int with_prev = 0; with_prev < 2; with_prev++) {
            double got = c2m_pitch_hz(pcm, N, 1200, with_prev ? f0 : 0.0);

            if (fabs(got / f0 - 1.0) > 0.02) {
                fprintf(stderr, "pitch of %.0f Hz harmonic signal is %.1f Hz (previous %s)\n", f0, got, with_prev ? "right" : "none");
                failed = 1;
            }
        }
    }
    return failed;
}

static void make_odd_weak(double odd)
{
    for (int i = 0; i < N; i++) {
        double t = (double)i / C2M_FS_HZ;
        double s = 0.0;

        for (int m = 1; m <= 12; m++) {
            double amp = (m % 2) != 0 ? odd : 1.0;

            s += amp * sin(2.0 * C2M_PI * 100.0 * (double)m * t + 0.5 * (double)m);
        }
        pcm[i] = (int16_t)floor(1500.0 * s + 0.5);
    }
}

static int expect_hz(const char *what, double got, double want)
{
    if (fabs(got / want - 1.0) > 0.04) {
        fprintf(stderr, "%s: the estimate is %.1f Hz, want %.0f\n", what, got, want);
        return 1;
    }
    return 0;
}

/* Tracking: a 100 Hz voice whose odd harmonics are weaker than the even ones,
 * so the strongest peak of the squared signal's spectrum is at 200 Hz and the
 * one at 100 Hz is weaker. Where it falls against the thresholds decides:
 *   odd 0.30: below the plain threshold, above the near-previous one.
 *   odd 0.35: above the plain threshold, below the far one.
 */
static int test_tracking(void)
{
    int failed = 0;

    make_odd_weak(0.30);
    failed |= expect_hz("odd 0.30, nothing before", c2m_pitch_hz(pcm, N, 1200, 0.0), 200.0);
    failed |= expect_hz("odd 0.30, 100 Hz before", c2m_pitch_hz(pcm, N, 1200, 100.0), 100.0);
    failed |= expect_hz("odd 0.30, 200 Hz before", c2m_pitch_hz(pcm, N, 1200, 200.0), 200.0);
    make_odd_weak(0.35);
    failed |= expect_hz("odd 0.35, nothing before", c2m_pitch_hz(pcm, N, 1200, 0.0), 100.0);
    failed |= expect_hz("odd 0.35, 100 Hz before", c2m_pitch_hz(pcm, N, 1200, 100.0), 100.0);
    failed |= expect_hz("odd 0.35, 200 Hz before", c2m_pitch_hz(pcm, N, 1200, 200.0), 200.0);
    return failed;
}

int main(void)
{
    int failed = 0;

    failed |= test_tilt();
    failed |= test_rules();
    failed |= test_pitch();
    failed |= test_tracking();
    if (failed == 0) {
        printf("rules: ok\n");
    }
    return failed;
}

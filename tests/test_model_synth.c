// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The c2fx Authors

#include "c2m_synth.h"

#include <math.h>
#include <stdio.h>

static const uint8_t example_frame[C2M_FRAME_BYTES] = {
    0xd6, 0x75, 0xef, 0x7b, 0xde, 0xf7, 0xbd, 0xef
};

static double rms(const int16_t *pcm, int n)
{
    double sum = 0.0;

    for (int i = 0; i < n; i++) {
        sum += (double)pcm[i] * (double)pcm[i];
    }
    return sqrt(sum / (double)n);
}

static int test_periodic(void)
{
    enum { k_fft = 2048 };
    c2m_indices idx;
    c2m_decode state;
    int16_t pcm[C2M_FRAME_SAMPLES];
    double samples[k_fft];
    double f0;
    double peak = 0.0;
    double expect_bin;
    int peak_bin = 0;
    int filled = 0;

    /* 159.4 Hz and a flat LSP code. The hop is not an integer number of
     * cycles, and harmonic 1 is the strongest line, so a stuck excitation
     * phase moves the DFT peak off f0.
     */
    idx.v_a = 1;
    idx.v_b = 1;
    idx.i = 40;
    idx.e = 22;
    for (int j = 0; j < C2M_LSP_ORDER; j++) {
        idx.d[j] = 0;
    }
    f0 = c2m_f0_hz(idx.i);
    c2m_decode_init(&state);
    for (int frame = 0; frame < 50; frame++) {
        c2m_decode_pcm(&state, &idx, pcm);
        if (frame < 20) {
            continue;
        }
        for (int n = 0; n < C2M_FRAME_SAMPLES && filled < k_fft; n++) {
            samples[filled] = (double)pcm[n];
            filled += 1;
        }
    }
    if (filled != k_fft) {
        fprintf(stderr, "periodic test filled %d samples\n", filled);
        return 1;
    }
    for (int k = 1; k < k_fft / 2; k++) {
        double wr = 0.0;
        double wi = 0.0;
        double ang = 2.0 * C2M_PI * (double)k / (double)k_fft;
        double mag;

        for (int n = 0; n < k_fft; n++) {
            wr += samples[n] * cos(ang * (double)n);
            wi -= samples[n] * sin(ang * (double)n);
        }
        mag = wr * wr + wi * wi;
        if (mag > peak) {
            peak = mag;
            peak_bin = k;
        }
    }
    expect_bin = f0 * (double)k_fft / C2M_FS_HZ;
    if (fabs((double)peak_bin - expect_bin) > 1.0) {
        fprintf(stderr, "periodic peak bin %d, f0 bin %.3f (f0 %.3f Hz)\n",
                peak_bin, expect_bin, f0);
        return 1;
    }
    {
        int lag = (int)floor(C2M_FS_HZ / f0 + 0.5);
        double corr = 0.0;
        double energy = 0.0;

        for (int n = 0; n < k_fft - lag; n++) {
            corr += samples[n] * samples[n + lag];
            energy += samples[n] * samples[n];
        }
        if (!(corr / energy > 0.9)) {
            fprintf(stderr, "periodic autocorrelation %.3f at lag %d\n", corr / energy, lag);
            return 1;
        }
    }
    return 0;
}

static int test_overlap_constant(void)
{
    double block[C2M_FRAME_SAMPLES];
    double ola[C2M_HOP];
    double out[C2M_HOP];

    for (int n = 0; n < C2M_FRAME_SAMPLES; n++) {
        block[n] = 1.0;
    }
    for (int n = 0; n < C2M_HOP; n++) {
        ola[n] = 0.0;
    }
    c2m_ola_hop(ola, block, out);
    if (!(fabs(out[0]) < 1e-9)) {
        fprintf(stderr, "overlap first sample is %.6g\n", out[0]);
        return 1;
    }
    c2m_ola_hop(ola, block, out);
    for (int n = 0; n < C2M_HOP; n++) {
        if (!(fabs(out[n] - 1.0) < 1e-9)) {
            fprintf(stderr, "overlap sample %d is %.12g\n", n, out[n]);
            return 1;
        }
    }
    c2m_ola_hop(ola, block, out);
    for (int n = 0; n < C2M_HOP; n++) {
        if (!(fabs(out[n] - 1.0) < 1e-9)) {
            fprintf(stderr, "overlap hold sample %d is %.12g\n", n, out[n]);
            return 1;
        }
    }
    return 0;
}

static int all_zero(const int16_t *pcm)
{
    for (int n = 0; n < C2M_FRAME_SAMPLES; n++) {
        if (pcm[n] != 0) {
            return 0;
        }
    }
    return 1;
}

static int test_lost(void)
{
    c2m_indices idx;
    c2m_decode state;
    int16_t pcm[C2M_FRAME_SAMPLES];
    double previous;

    if (c2m_frame_unpack(example_frame, &idx) != 0) {
        return 1;
    }
    idx.e = 28;
    c2m_decode_init(&state);
    for (int frame = 0; frame < 8; frame++) {
        c2m_decode_pcm(&state, &idx, pcm);
    }
    previous = rms(pcm, C2M_FRAME_SAMPLES);
    for (int lost = 0; lost < C2M_LOST_MAX_FRAMES; lost++) {
        double level;

        c2m_decode_pcm(&state, NULL, pcm);
        level = rms(pcm, C2M_FRAME_SAMPLES);
        if (!(level < previous)) {
            fprintf(stderr, "lost frame %d rms %.6g did not fall from %.6g\n",
                    lost + 1, level, previous);
            return 1;
        }
        previous = level;
    }
    for (int extra = 0; extra < 2; extra++) {
        c2m_decode_pcm(&state, NULL, pcm);
        if (!all_zero(pcm)) {
            fprintf(stderr, "lost frame after the limit is not silence\n");
            return 1;
        }
    }
    return 0;
}

static int test_deterministic(void)
{
    c2m_indices idx;
    c2m_decode left;
    c2m_decode right;
    int16_t a[C2M_FRAME_SAMPLES];
    int16_t b[C2M_FRAME_SAMPLES];
    int nonzero = 0;

    if (c2m_frame_unpack(example_frame, &idx) != 0) {
        return 1;
    }
    c2m_decode_init(&left);
    c2m_decode_init(&right);
    for (int frame = 0; frame < 12; frame++) {
        const c2m_indices *use = &idx;

        if (frame == 3) {
            idx.v_a = 0;
            idx.v_b = 0;
        } else if (frame == 4) {
            idx.v_a = 1;
            idx.v_b = 1;
        }
        if (frame == 5) {
            use = NULL;
        }
        c2m_decode_pcm(&left, use, a);
        c2m_decode_pcm(&right, use, b);
        for (int n = 0; n < C2M_FRAME_SAMPLES; n++) {
            if (a[n] != b[n]) {
                fprintf(stderr, "frame %d sample %d is %d and %d\n", frame, n, a[n], b[n]);
                return 1;
            }
            if (a[n] != 0) {
                nonzero = 1;
            }
        }
    }
    if (!nonzero) {
        fprintf(stderr, "deterministic output is silent\n");
        return 1;
    }
    return 0;
}

int main(void)
{
    if (test_periodic() != 0 || test_overlap_constant() != 0 || test_lost() != 0 ||
        test_deterministic() != 0) {
        return 1;
    }
    return 0;
}

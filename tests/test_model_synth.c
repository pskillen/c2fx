// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The c2fx Authors

#include "c2m_synth.h"

#include <math.h>
#include <stdio.h>

static const uint8_t example_frame[C2M_FRAME_BYTES] = {
    0xd6, 0x75, 0xef, 0x7b, 0xde, 0xf7, 0xbd, 0xef
};

static int hook_noise(int harmonic, double amp, double peak, void *user);

static double rms(const int16_t *pcm, int n)
{
    double sum = 0.0;

    for (int i = 0; i < n; i++) {
        sum += (double)pcm[i] * (double)pcm[i];
    }
    return sqrt(sum / (double)n);
}

static double lpc_angle(const double a[C2M_LPC_ORDER + 1], int bin)
{
    double angle = 2.0 * C2M_PI * (double)bin / (double)C2M_DFT_SIZE;
    double re = 0.0;
    double im = 0.0;

    for (int k = 0; k <= C2M_LPC_ORDER; k++) {
        re += a[k] * cos(angle * (double)k);
        im -= a[k] * sin(angle * (double)k);
    }
    return -atan2(im, re);
}

static double wrap_pi_open(double x)
{
    while (x > C2M_PI) {
        x -= 2.0 * C2M_PI;
    }
    while (x <= -C2M_PI) {
        x += 2.0 * C2M_PI;
    }
    return x;
}

static int test_min_phase_lpc(void)
{
    c2m_indices idx;
    c2m_params params;
    double a[C2M_LPC_ORDER + 1];
    double power[C2M_SPECTRUM_BINS];
    double phase[C2M_SPECTRUM_BINS + 1];

    if (c2m_frame_unpack(example_frame, &idx) != 0) {
        return 1;
    }
    c2m_dequant(&idx, &params);
    c2m_lsp_to_lpc(params.lsp_rad, a);
    c2m_lpc_power(a, power);
    c2m_min_phase(power, phase);
    for (int bin = 1; bin <= C2M_SPECTRUM_BINS; bin++) {
        double expect = lpc_angle(a, bin);
        double err = fabs(wrap_pi_open(phase[bin] - expect));

        if (err > 1e-3) {
            fprintf(stderr, "min phase bin %d err %.3e rad\n", bin, err);
            return 1;
        }
    }
    return 0;
}

static int collect_steady_pcm(const c2m_indices *idx, double samples[], int target)
{
    c2m_decode state;
    int16_t pcm[C2M_FRAME_SAMPLES];
    int filled = 0;

    c2m_decode_init(&state);
    for (int frame = 0; frame < 50; frame++) {
        c2m_decode_pcm(&state, idx, pcm);
        if (frame < 20) {
            continue;
        }
        for (int n = 0; n < C2M_FRAME_SAMPLES && filled < target; n++) {
            samples[filled] = (double)pcm[n];
            filled += 1;
        }
    }
    return filled == target ? 0 : 1;
}

static int test_phi1_scaling(void)
{
    enum { k_fft = 2048 };
    c2m_indices idx;
    double samples[k_fft];
    double f0;
    double peak = 0.0;
    double expect_bin;
    int peak_bin = 0;

    if (c2m_frame_unpack(example_frame, &idx) != 0) {
        return 1;
    }
    idx.v_a = 1;
    idx.v_b = 1;
    idx.i = 40;
    idx.e = 22;
    if (collect_steady_pcm(&idx, samples, k_fft) != 0) {
        return 1;
    }
    f0 = c2m_f0_hz(idx.i);
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
    if (fabs((double)peak_bin - expect_bin) > 0.35) {
        fprintf(stderr, "phi1 peak bin %d, f0 bin %.3f (f0 %.3f Hz)\n",
                peak_bin, expect_bin, f0);
        return 1;
    }
    return 0;
}

static int test_periodic(void)
{
    enum { k_fft = 2048 };
    c2m_indices idx;
    double samples[k_fft];
    double f0;

    if (c2m_frame_unpack(example_frame, &idx) != 0) {
        return 1;
    }
    if (collect_steady_pcm(&idx, samples, k_fft) != 0) {
        return 1;
    }
    f0 = c2m_f0_hz(idx.i);
    {
        int lag = (int)floor(C2M_FS_HZ / f0 + 0.5);
        double corr = 0.0;
        double energy = 0.0;
        double peak_sample = 0.0;
        double rms_sample = 0.0;

        for (int n = 0; n < k_fft; n++) {
            double a = fabs(samples[n]);

            if (a > peak_sample) {
                peak_sample = a;
            }
            rms_sample += samples[n] * samples[n];
        }
        rms_sample = sqrt(rms_sample / (double)k_fft);
        if (!(peak_sample / rms_sample > 3.0)) {
            fprintf(stderr, "periodic crest factor %.3f\n", peak_sample / rms_sample);
            return 1;
        }
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

static int test_voiced_rng_idle(void)
{
    c2m_indices idx;
    c2m_decode state;
    int16_t pcm[C2M_FRAME_SAMPLES];
    uint32_t before;
    uint32_t after;

    if (c2m_frame_unpack(example_frame, &idx) != 0) {
        return 1;
    }
    idx.v_a = 1;
    idx.v_b = 1;
    c2m_decode_init(&state);
    for (int frame = 0; frame < 4; frame++) {
        c2m_decode_pcm(&state, &idx, pcm);
    }
    before = state.rng;
    c2m_decode_pcm(&state, &idx, pcm);
    after = state.rng;
    if (before != after) {
        fprintf(stderr, "voiced frame consumed rng %u -> %u\n", before, after);
        return 1;
    }
    idx.v_a = 0;
    idx.v_b = 0;
    before = state.rng;
    c2m_decode_pcm(&state, &idx, pcm);
    after = state.rng;
    if (before == after) {
        fprintf(stderr, "unvoiced frame left rng unchanged\n");
        return 1;
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
    enum { k_decay_frames = 4, k_lost_tail = 5, k_silent_after = 6 };
    c2m_indices idx;
    c2m_decode state;
    int16_t pcm[C2M_FRAME_SAMPLES];
    double previous;
    double level;

    if (c2m_frame_unpack(example_frame, &idx) != 0) {
        return 1;
    }
    idx.e = 28;
    c2m_decode_init(&state);
    for (int frame = 0; frame < 8; frame++) {
        c2m_decode_pcm(&state, &idx, pcm);
    }
    previous = rms(pcm, C2M_FRAME_SAMPLES);
    for (int lost = 1; lost <= k_decay_frames; lost++) {
        double drop_db;

        c2m_decode_pcm(&state, NULL, pcm);
        level = rms(pcm, C2M_FRAME_SAMPLES);
        drop_db = 20.0 * log10(level / previous);
        if (!(drop_db < -1.5)) {
            fprintf(stderr, "lost frame %d drop %.3f dB from %.6g to %.6g\n",
                    lost, drop_db, previous, level);
            return 1;
        }
        previous = level;
    }
    c2m_decode_pcm(&state, NULL, pcm);
    level = rms(pcm, C2M_FRAME_SAMPLES);
    if (!(level > 0.0 && level < previous)) {
        fprintf(stderr, "lost frame %d tail rms %.6g, previous %.6g\n",
                k_lost_tail, level, previous);
        return 1;
    }
    c2m_decode_pcm(&state, NULL, pcm);
    if (!all_zero(pcm)) {
        fprintf(stderr, "lost frame %d is not silence\n", k_silent_after);
        return 1;
    }
    return 0;
}

static int test_saturation_rails(void)
{
    c2m_indices idx;
    c2m_decode state;
    int16_t pcm[C2M_FRAME_SAMPLES];
    int saw_pos = 0;
    int saw_neg = 0;

    idx.v_a = 1;
    idx.v_b = 1;
    idx.i = 40;
    idx.e = 31;
    for (int j = 0; j < C2M_LSP_ORDER; j++) {
        idx.d[j] = 0;
    }
    c2m_decode_init(&state);
    for (int frame = 0; frame < 30; frame++) {
        c2m_decode_pcm(&state, &idx, pcm);
    }
    for (int n = 0; n < C2M_FRAME_SAMPLES; n++) {
        if (pcm[n] == 32767) {
            saw_pos = 1;
        }
        if (pcm[n] == -32767) {
            saw_neg = 1;
        }
    }
    if (!saw_pos || !saw_neg) {
        fprintf(stderr, "saturation rails pos %d neg %d\n", saw_pos, saw_neg);
        return 1;
    }
    return 0;
}

static int test_lost_noise_floor_hook(void)
{
    c2m_indices idx;
    c2m_decode state;
    int16_t pcm[C2M_FRAME_SAMPLES];

    if (c2m_frame_unpack(example_frame, &idx) != 0) {
        return 1;
    }
    c2m_decode_init(&state);
    state.noise_floor = hook_noise;
    state.noise_floor_user = NULL;
    for (int frame = 0; frame < 6; frame++) {
        c2m_decode_pcm(&state, &idx, pcm);
    }
    for (int lost = 0; lost < C2M_LOST_MAX_FRAMES + 2; lost++) {
        c2m_decode_pcm(&state, NULL, pcm);
    }
    if (state.noise_floor == NULL) {
        fprintf(stderr, "noise-floor hook cleared after lost run\n");
        return 1;
    }
    return 0;
}

static int hook_noise(int harmonic, double amp, double peak, void *user)
{
    (void)harmonic;
    (void)amp;
    (void)peak;
    (void)user;
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

/* Phase relation of spec 7: harmonic m has phase m * phi1 + minphase(bin m), so
 * phase(m) - m * phase(1) = minphase(m) - m * minphase(1), whatever phi1 is.
 * Harmonic phase is read from the pre-OLA block (centered on index 80) with a
 * Hann-weighted projection onto each harmonic's own frequency. Leakage from the
 * neighbors and the 160-sample window set the tolerance (measured 0.024 rad
 * worst case on this frame, limit 0.1; a wrong phase model is off by radians).
 */
static double wrap_pi(double x)
{
    while (x > C2M_PI) {
        x -= 2.0 * C2M_PI;
    }
    while (x < -C2M_PI) {
        x += 2.0 * C2M_PI;
    }
    return x;
}

static double block_phase(const double block[C2M_FRAME_SAMPLES], int bin)
{
    double omega = 2.0 * C2M_PI * (double)bin / (double)C2M_DFT_SIZE;
    double re = 0.0;
    double im = 0.0;

    for (int n = -C2M_HOP; n < C2M_HOP; n++) {
        double w = 0.5 + 0.5 * cos(C2M_PI * (double)n / (double)C2M_HOP);
        double x = block[n + C2M_HOP] * w;

        re += x * cos(omega * (double)n);
        im -= x * sin(omega * (double)n);
    }
    return atan2(im, re);
}

static int test_harmonic_phase(void)
{
    c2m_indices idx;
    c2m_decode st;
    c2m_subframe sub[2];
    double a[C2M_LPC_ORDER + 1];
    double power[C2M_SPECTRUM_BINS];
    double minphase[C2M_SPECTRUM_BINS + 1];
    double block[C2M_FRAME_SAMPLES];
    int bin1;
    int worst_bin = 0;
    double worst = 0.0;

    idx.v_a = 1;
    idx.v_b = 1;
    idx.i = 40;
    idx.e = 22;
    for (int j = 0; j < C2M_LSP_ORDER; j++) {
        idx.d[j] = 0;
    }
    c2m_decode_init(&st);
    c2m_decode_frame(&st, &idx, sub);
    c2m_lsp_to_lpc(sub[0].lsp_rad, a);
    c2m_lpc_power(a, power);
    c2m_min_phase(power, minphase);
    bin1 = c2m_harm_bin(1, sub[0].w0);
    for (int blk = 0; blk < 12; blk++) {
        double ph1;

        c2m_synth_block(&st, &sub[0], block);
        ph1 = block_phase(block, bin1);
        for (int m = 2; m <= 6; m++) {
            int bin = c2m_harm_bin(m, sub[0].w0);
            double got = wrap_pi(block_phase(block, bin) - (double)m * ph1);
            double expect = wrap_pi(minphase[bin] - (double)m * minphase[bin1]);
            double err = fabs(wrap_pi(got - expect));

            if (err > worst) {
                worst = err;
                worst_bin = bin;
            }
        }
    }
    if (worst > 0.1) {
        fprintf(stderr, "harmonic phase off by %.3f rad at bin %d (limit 0.1)\n", worst, worst_bin);
        return 1;
    }
    return 0;
}

/* Round half up of m * f0 / 15.625. Grid f0 = (6400 + 350*i) / 128,
 * so the bin is m * (6400 + 350*i) / 2000. A midpoint of two indices is
 * m * (12800 + 350*(i_prev + i_cur)) / 4000.
 */
static int exact_harm_bin(int m, int key, int midpoint)
{
    if (midpoint) {
        return (m * (12800 + 350 * key) + 2000) / 4000;
    }
    return (m * (6400 + 350 * key) + 1000) / 2000;
}

static int harm_bin_matches(double w0, int m, int key, int midpoint)
{
    int got = c2m_harm_bin(m, w0);
    int expect = exact_harm_bin(m, key, midpoint);

    if (got == expect) {
        return 1;
    }
    if (midpoint) {
        fprintf(stderr, "harm bin midpoint key %d m %d got %d expect %d\n", key, m, got, expect);
    } else {
        fprintf(stderr, "harm bin index %d m %d got %d expect %d\n", key, m, got, expect);
    }
    return 0;
}

static int test_harm_bins(void)
{
    for (int i = 0; i < 128; i++) {
        double w0 = c2m_w0_rad(i);
        int count = c2m_harmonic_count(w0);

        for (int m = 1; m <= count; m++) {
            if (!harm_bin_matches(w0, m, i, 0)) {
                return 1;
            }
        }
    }
    for (int i0 = 0; i0 < 128; i0++) {
        for (int i1 = 0; i1 < 128; i1++) {
            double w0 = 0.5 * (c2m_w0_rad(i0) + c2m_w0_rad(i1));
            int key = i0 + i1;
            int count = c2m_harmonic_count(w0);

            for (int m = 1; m <= count; m++) {
                if (!harm_bin_matches(w0, m, key, 1)) {
                    return 1;
                }
            }
        }
    }
    printf("harmonic bins: 128 grid indices and every midpoint pair match the integer bin\n");
    return 0;
}

int main(void)
{
    if (test_harm_bins() != 0 || test_min_phase_lpc() != 0 || test_phi1_scaling() != 0 ||
        test_harmonic_phase() != 0 || test_periodic() != 0 ||
        test_voiced_rng_idle() != 0 || test_overlap_constant() != 0 || test_lost() != 0 ||
        test_saturation_rails() != 0 || test_lost_noise_floor_hook() != 0 || test_deterministic() != 0) {
        return 1;
    }
    return 0;
}

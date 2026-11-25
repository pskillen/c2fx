// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The c2fx Authors

#include "c2m_decode.h"
#include "c2m_lpc.h"

#include <math.h>
#include <stdio.h>
#include <stdint.h>

static uint32_t xorshift32(uint32_t *state)
{
    uint32_t x = *state;

    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static int same_indices(const c2m_indices *a, const c2m_indices *b)
{
    if (a->v_a != b->v_a || a->v_b != b->v_b || a->i != b->i || a->e != b->e) {
        return 0;
    }
    for (int j = 0; j < C2M_LSP_ORDER; j++) {
        if (a->d[j] != b->d[j]) {
            return 0;
        }
    }
    return 1;
}

static int test_gray(void)
{
    for (unsigned n = 0; n < 32u; n++) {
        if (c2m_gray_encode(n) != (n ^ (n >> 1))) {
            fprintf(stderr, "5-bit Gray encode of %u is %u\n", n, c2m_gray_encode(n));
            return 1;
        }
        if (c2m_gray_decode(c2m_gray_encode(n)) != n) {
            fprintf(stderr, "5-bit Gray round trip failed for %u\n", n);
            return 1;
        }
    }
    for (unsigned n = 0; n < 128u; n++) {
        if (c2m_gray_decode(c2m_gray_encode(n)) != n) {
            fprintf(stderr, "7-bit Gray round trip failed for %u\n", n);
            return 1;
        }
    }
    return 0;
}

static int test_random(void)
{
    uint32_t state = 0xC2F03200u;

    for (int n = 0; n < 100000; n++) {
        c2m_indices idx;
        c2m_indices got;
        uint8_t packed[C2M_FRAME_BYTES];

        idx.v_a = (int)(xorshift32(&state) & 1u);
        idx.v_b = (int)(xorshift32(&state) & 1u);
        idx.i = (int)(xorshift32(&state) & 127u);
        idx.e = (int)(xorshift32(&state) & 31u);
        for (int j = 0; j < C2M_LSP_ORDER; j++) {
            idx.d[j] = (int)(xorshift32(&state) & 31u);
        }
        if (c2m_frame_pack(&idx, packed) != 0) {
            fprintf(stderr, "pack failed on random frame %d\n", n);
            return 1;
        }
        if (c2m_frame_unpack(packed, &got) != 0 || !same_indices(&idx, &got)) {
            fprintf(stderr, "pack/unpack round trip failed on random frame %d\n", n);
            return 1;
        }
    }
    return 0;
}

static int test_example(void)
{
    static const uint8_t bytes[C2M_FRAME_BYTES] = {
        0xd6, 0x75, 0xef, 0x7b, 0xde, 0xf7, 0xbd, 0xef
    };
    c2m_indices idx;
    uint8_t packed[C2M_FRAME_BYTES];
    double f0;

    if (c2m_frame_unpack(bytes, &idx) != 0) {
        fprintf(stderr, "worked example did not unpack\n");
        return 1;
    }
    if (idx.v_a != 1 || idx.v_b != 1 || idx.i != 55 || idx.e != 22) {
        fprintf(stderr,
                "worked example is v=%d,%d i=%d e=%d\n",
                idx.v_a, idx.v_b, idx.i, idx.e);
        return 1;
    }
    for (int j = 0; j < C2M_LSP_ORDER; j++) {
        if (idx.d[j] != 10) {
            fprintf(stderr, "worked example d%d is %d, expected 10\n", j + 1, idx.d[j]);
            return 1;
        }
    }

    /* Spec 3.2. 200.390625 Hz prints as 200.39 Hz to 0.01 Hz. */
    f0 = c2m_f0_hz(idx.i);
    if (fabs(f0 - 200.39) >= 0.005) {
        fprintf(stderr, "worked example f0 is %.6f Hz, expected 200.39 Hz\n", f0);
        return 1;
    }
    if (c2m_frame_pack(&idx, packed) != 0) {
        fprintf(stderr, "worked example did not pack\n");
        return 1;
    }
    for (int i = 0; i < C2M_FRAME_BYTES; i++) {
        if (packed[i] != bytes[i]) {
            fprintf(stderr, "worked example byte %d is 0x%02x, expected 0x%02x\n",
                    i, packed[i], bytes[i]);
            return 1;
        }
    }
    return 0;
}

static int near(const char *what, double got, double expect, double tol)
{
    if (fabs(got - expect) > tol) {
        fprintf(stderr, "%s is %.12g, expected %.12g\n", what, got, expect);
        return 1;
    }
    return 0;
}

static void set_indices(c2m_indices *idx, int v_a, int v_b, int i, int e, int d)
{
    idx->v_a = v_a;
    idx->v_b = v_b;
    idx->i = i;
    idx->e = e;
    for (int j = 0; j < C2M_LSP_ORDER; j++) {
        idx->d[j] = d;
    }
}

static int test_dequant(void)
{
    static const uint8_t bytes[C2M_FRAME_BYTES] = {
        0xd6, 0x75, 0xef, 0x7b, 0xde, 0xf7, 0xbd, 0xef
    };
    static const double lsp_hz[C2M_LSP_ORDER] = {
        275, 550, 825, 1175, 1525, 1875, 2150, 2425, 2700, 2975
    };
    c2m_indices idx;
    c2m_indices wide;
    c2m_params params;
    double wmin = 2.0 * C2M_PI / 160.0;
    double wmax = 2.0 * C2M_PI / 20.0;

    if (c2m_frame_unpack(bytes, &idx) != 0) {
        return 1;
    }
    c2m_dequant(&idx, &params);
    if (near("example f0", params.f0_hz, c2m_f0_hz(55), 0.0) != 0) {
        return 1;
    }
    if (near("example energy", params.energy_db, C2M_ENERGY_STEP_DB * 22.0, 0.0) != 0) {
        return 1;
    }
    if (near("example w0", params.w0, 2.0 * C2M_PI * params.f0_hz / C2M_FS_HZ, 1e-12) != 0) {
        return 1;
    }
    if (near("w0 grid", c2m_w0_rad(55), wmin + 55.0 * (wmax - wmin) / 128.0, 1e-12) != 0) {
        return 1;
    }
    if (near("f0 index 0", c2m_f0_hz(0), 50.0, 0.0) != 0 ||
        near("f0 index 127", c2m_f0_hz(127), 397.265625, 1e-9) != 0) {
        return 1;
    }
    for (int j = 0; j < C2M_LSP_ORDER; j++) {
        if (near("example lsp", params.lsp_hz[j], lsp_hz[j], 0.0) != 0) {
            return 1;
        }
        if (near("example lsp rad", params.lsp_rad[j],
                 2.0 * C2M_PI * lsp_hz[j] / C2M_FS_HZ, 1e-12) != 0) {
            return 1;
        }
    }

    /* Spec 3.4: the maximum running sum is 9800 Hz, and it is left as-is. */
    set_indices(&wide, 1, 1, 0, 0, 31);
    c2m_dequant(&wide, &params);
    if (near("max lsp sum", params.lsp_hz[C2M_LSP_ORDER - 1], 9800.0, 0.0) != 0) {
        return 1;
    }
    return 0;
}

static int test_interpolate(void)
{
    c2m_decode st;
    c2m_indices idx;
    c2m_subframe sub[2];
    c2m_params low;
    c2m_params high;
    double energy_20 = C2M_ENERGY_STEP_DB * 20.0;

    c2m_decode_init(&st);
    set_indices(&idx, 1, 1, 0, 20, 0);
    c2m_dequant(&idx, &low);
    c2m_decode_frame(&st, &idx, sub);
    if (sub[0].voiced != 1 || sub[1].voiced != 1) {
        fprintf(stderr, "startup voicing is %d,%d\n", sub[0].voiced, sub[1].voiced);
        return 1;
    }
    if (near("startup energy a", sub[0].energy_db, 0.5 * energy_20, 1e-12) != 0 ||
        near("startup energy b", sub[1].energy_db, energy_20, 1e-12) != 0 ||
        near("startup w0 a", sub[0].w0, low.w0, 0.0) != 0) {
        return 1;
    }
    for (int j = 0; j < C2M_LSP_ORDER; j++) {
        double startup_hz = (double)j * C2M_FS_HZ / 22.0;

        if (near("startup lsp a", sub[0].lsp_hz[j], 0.5 * (startup_hz + low.lsp_hz[j]), 1e-12) != 0) {
            return 1;
        }
    }

    set_indices(&idx, 1, 1, 127, 0, 10);
    c2m_dequant(&idx, &high);
    c2m_decode_frame(&st, &idx, sub);
    if (near("voiced w0 a", sub[0].w0, 0.5 * (low.w0 + high.w0), 1e-12) != 0 ||
        near("voiced energy a", sub[0].energy_db, 0.5 * energy_20, 1e-12) != 0 ||
        near("voiced w0 b", sub[1].w0, high.w0, 0.0) != 0) {
        return 1;
    }
    for (int j = 0; j < C2M_LSP_ORDER; j++) {
        if (near("voiced lsp a", sub[0].lsp_hz[j],
                 0.5 * (low.lsp_hz[j] + high.lsp_hz[j]), 1e-12) != 0) {
            return 1;
        }
    }

    /* Previous b voiced, sub-frame a voiced, b unvoiced: blend w0 on a only. */
    set_indices(&idx, 1, 0, 40, 10, 0);
    c2m_dequant(&idx, &low);
    c2m_decode_frame(&st, &idx, sub);
    if (sub[0].voiced != 1 || sub[1].voiced != 0) {
        fprintf(stderr, "mixed voicing is %d,%d\n", sub[0].voiced, sub[1].voiced);
        return 1;
    }
    if (near("voiced-a unvoiced-b w0 a", sub[0].w0, 0.5 * (high.w0 + low.w0), 1e-12) != 0 ||
        near("voiced-a unvoiced-b w0 b", sub[1].w0, low.w0, 0.0) != 0) {
        return 1;
    }

    /* Previous b voiced, sub-frame a unvoiced: copy current w0. */
    set_indices(&idx, 0, 1, 60, 10, 0);
    c2m_dequant(&idx, &low);
    c2m_decode_frame(&st, &idx, sub);
    if (sub[0].voiced != 0 || sub[1].voiced != 1) {
        fprintf(stderr, "unvoiced-a voicing is %d,%d\n", sub[0].voiced, sub[1].voiced);
        return 1;
    }
    if (near("unvoiced-a w0 a", sub[0].w0, low.w0, 0.0) != 0 ||
        near("unvoiced-a w0 b", sub[1].w0, low.w0, 0.0) != 0) {
        return 1;
    }
    if (fabs(sub[0].w0 - 0.5 * (high.w0 + low.w0)) < 1e-6) {
        fprintf(stderr, "unvoiced-a w0 a blended\n");
        return 1;
    }
    return 0;
}

static int test_lpc_envelope(void)
{
    static const uint8_t bytes[C2M_FRAME_BYTES] = {
        0xd6, 0x75, 0xef, 0x7b, 0xde, 0xf7, 0xbd, 0xef
    };
    static const double expect_a[C2M_LPC_ORDER + 1] = {
        1.0000000000000000e+00,
        -2.0875197867998265e+00,
        2.8871893991494755e+00,
        -3.5202319085810885e+00,
        3.6477991707337312e+00,
        -3.2432717041757630e+00,
        2.5676863562437702e+00,
        -1.7484554414783167e+00,
        9.8127244229526478e-01,
        -4.1200142761961889e-01,
        9.0477641201940573e-02
    };
    c2m_indices idx;
    c2m_params params;
    c2m_envelope env;
    double a[C2M_LPC_ORDER + 1];

    if (c2m_frame_unpack(bytes, &idx) != 0) {
        return 1;
    }
    c2m_dequant(&idx, &params);
    c2m_lsp_to_lpc(params.lsp_rad, a);
    for (int k = 0; k <= C2M_LPC_ORDER; k++) {
        if (near("lpc coefficient", a[k], expect_a[k], 1e-8) != 0) {
            return 1;
        }
    }
    if (c2m_harmonic_count(params.w0) != 19) {
        fprintf(stderr, "example L is %d, expected 19\n", c2m_harmonic_count(params.w0));
        return 1;
    }
    if (c2m_harmonic_count(c2m_w0_rad(0)) != 80) {
        fprintf(stderr, "50 Hz L is %d, expected 80\n", c2m_harmonic_count(c2m_w0_rad(0)));
        return 1;
    }

    c2m_envelope_sample(a, params.w0, params.energy_db, &env);
    if (env.count != 19) {
        fprintf(stderr, "envelope L is %d, expected 19\n", env.count);
        return 1;
    }
/* Goldens are for the default beta pair; make test EXTRA_CFLAGS=-DC2M_POSTFILTER_PDF skips this. */
#ifndef C2M_POSTFILTER_PDF
    if (near("harmonic 1 amplitude", env.amp[0], 520.01713855477533, 1e-4) != 0) {
        return 1;
    }
#endif
#ifndef C2M_POSTFILTER_PDF
    {
        double sum_sq = 0.0;

        for (int m = 0; m < env.count; m++) {
            sum_sq += env.amp[m] * env.amp[m];
        }
        if (near("level sum", sum_sq, 944672.47528305487, 1e-2) != 0) {
            return 1;
        }
    }

    c2m_envelope_sample(a, c2m_w0_rad(36), params.energy_db, &env);
    if (near("harmonic 1 index 36", env.amp[0], 13.974406431593373, 1e-4) != 0) {
        return 1;
    }
    c2m_envelope_sample(a, c2m_w0_rad(37), params.energy_db, &env);
    if (near("harmonic 1 index 37", env.amp[0], 459.68444981168255, 1e-3) != 0) {
        return 1;
    }
#endif
    {
        double w0_150 = C2M_PI * 150.0 / 4000.0;
        double amp_unscaled;

        c2m_envelope_sample(a, w0_150 + 1e-12, params.energy_db, &env);
        amp_unscaled = env.amp[0];
        c2m_envelope_sample(a, w0_150 - 1e-12, params.energy_db, &env);
        if (near("harmonic 1 below 150 Hz", env.amp[0], C2M_HARM1_SCALE * amp_unscaled, 1e-4) != 0) {
            return 1;
        }
        c2m_envelope_sample(a, w0_150, params.energy_db, &env);
        if (near("harmonic 1 at 150 Hz", env.amp[0], amp_unscaled, 1e-4) != 0) {
            return 1;
        }
    }
    c2m_envelope_sample(a, c2m_w0_rad(0), params.energy_db, &env);
    if (env.count != 80 || !(env.amp[1] > 0.0)) {
        fprintf(stderr, "50 Hz envelope count %d amp0 %.6g amp1 %.6g\n",
                env.count, env.amp[0], env.amp[1]);
        return 1;
    }
#ifndef C2M_POSTFILTER_PDF
    if (near("50 Hz harmonic 1", env.amp[0], 8.1396655893986676, 1e-3) != 0) {
        return 1;
    }

    c2m_envelope_sample(a, c2m_w0_rad(100), params.energy_db, &env);
    {
        double sum_sq = 0.0;

        for (int m = 0; m < env.count; m++) {
            sum_sq += env.amp[m] * env.amp[m];
        }
        if (env.count != 12 || near("high pitch band sum", sum_sq, 859279.58720901445, 0.5) != 0) {
            fprintf(stderr, "index 100 envelope L %d sum %.12f\n", env.count, sum_sq);
            return 1;
        }
    }
#else
    c2m_envelope_sample(a, c2m_w0_rad(100), params.energy_db, &env);
    if (env.count != 12) {
        fprintf(stderr, "index 100 envelope L %d\n", env.count);
        return 1;
    }
#endif

    return 0;
}

static int test_lsp_group_order(void)
{
    c2m_indices idx;
    c2m_params params;

    idx.v_a = 1;
    idx.v_b = 1;
    idx.i = 55;
    idx.e = 10;
    for (int j = 0; j < C2M_LSP_ORDER; j++) {
        idx.d[j] = j;
    }
    c2m_dequant(&idx, &params);
    /* d[j]=j: ten distinct indices; group j uses table row j. */
    if (near("ordered lsp 0", params.lsp_hz[0], 25.0, 0.0) != 0 ||
        near("ordered lsp 5", params.lsp_hz[5], 525.0, 0.0) != 0 ||
        near("ordered lsp 9", params.lsp_hz[9], 1375.0, 0.0) != 0) {
        return 1;
    }
    return 0;
}

static int test_pack_rejects(void)
{
    c2m_indices idx;
    uint8_t packed[C2M_FRAME_BYTES];

    idx.v_a = 1;
    idx.v_b = 1;
    idx.i = 128;
    idx.e = 0;
    for (int j = 0; j < C2M_LSP_ORDER; j++) {
        idx.d[j] = 0;
    }
    if (c2m_frame_pack(&idx, packed) == 0) {
        fprintf(stderr, "pack accepted a w0 index of 128\n");
        return 1;
    }
    return 0;
}

int main(void)
{
    if (test_gray() != 0 || test_random() != 0 || test_example() != 0 ||
        test_dequant() != 0 || test_interpolate() != 0 || test_lpc_envelope() != 0 ||
        test_lsp_group_order() != 0 || test_pack_rejects() != 0) {
        return 1;
    }
    return 0;
}

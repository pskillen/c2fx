// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The c2fx Authors

#include "c2m_decode.h"

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
        near("startup w0 a", sub[0].w0, low.w0, 0.0) != 0 ||
        near("startup lsp a", sub[0].lsp_hz[9], low.lsp_hz[9], 0.0) != 0) {
        return 1;
    }

    set_indices(&idx, 1, 1, 127, 0, 10);
    c2m_dequant(&idx, &high);
    c2m_decode_frame(&st, &idx, sub);
    if (near("voiced w0 a", sub[0].w0, 0.5 * (low.w0 + high.w0), 1e-12) != 0 ||
        near("voiced energy a", sub[0].energy_db, 0.5 * energy_20, 1e-12) != 0 ||
        near("voiced lsp a", sub[0].lsp_hz[0], 0.5 * (low.lsp_hz[0] + high.lsp_hz[0]), 1e-12) != 0 ||
        near("voiced w0 b", sub[1].w0, high.w0, 0.0) != 0) {
        return 1;
    }

    /* Previous b is voiced, this b is not: do not blend w0. */
    set_indices(&idx, 1, 0, 40, 10, 0);
    c2m_dequant(&idx, &low);
    c2m_decode_frame(&st, &idx, sub);
    if (sub[0].voiced != 1 || sub[1].voiced != 0) {
        fprintf(stderr, "mixed voicing is %d,%d\n", sub[0].voiced, sub[1].voiced);
        return 1;
    }
    if (near("unvoiced w0 a", sub[0].w0, low.w0, 0.0) != 0 ||
        near("unvoiced w0 b", sub[1].w0, low.w0, 0.0) != 0) {
        return 1;
    }
    if (fabs(sub[0].w0 - 0.5 * (high.w0 + low.w0)) < 1e-6) {
        fprintf(stderr, "unvoiced w0 a blended\n");
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
        test_dequant() != 0 || test_interpolate() != 0 || test_pack_rejects() != 0) {
        return 1;
    }
    return 0;
}

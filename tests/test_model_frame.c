// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The c2fx Authors

#include "c2m_frame.h"

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
    f0 = 50.0 + 350.0 * (double)idx.i / 128.0;
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
        test_pack_rejects() != 0) {
        return 1;
    }
    return 0;
}

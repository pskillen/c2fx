// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The c2fx Authors
// Spec 2: 64-bit frame, bit 0 is the MSB of byte 0, binary-reflected Gray.

#include "c2fx_internal.h"

static int bit_at(const uint8_t in[8], int bit)
{
    return (in[bit >> 3] >> (7 - (bit & 7))) & 1;
}

static unsigned get_field(const uint8_t in[8], int bit, int width)
{
    unsigned value = 0;

    for (int i = 0; i < width; i++) {
        value = (value << 1) | (unsigned)bit_at(in, bit + i);
    }
    return value;
}

static unsigned gray_decode(unsigned gray)
{
    unsigned n = gray;

    for (unsigned shifted = gray >> 1; shifted != 0u; shifted >>= 1) {
        n ^= shifted;
    }
    return n;
}

void c2fx_frame_unpack(const uint8_t in[8], c2fx_indices *idx)
{
    idx->v_a = (int)get_field(in, 0, 1);
    idx->v_b = (int)get_field(in, 1, 1);
    idx->i = (int)gray_decode(get_field(in, 2, 7));
    idx->e = (int)gray_decode(get_field(in, 9, 5));
    for (int j = 0; j < C2FX_LSP_ORDER; j++) {
        idx->d[j] = (int)gray_decode(get_field(in, 14 + 5 * j, 5));
    }
}

#ifndef C2FX_NO_ENCODER
static void put_field(uint8_t out[8], int bit, int width, unsigned value)
{
    value ^= value >> 1;
    for (int i = width - 1; i >= 0; i--, bit++) {
        out[bit >> 3] |= (uint8_t)(((value >> i) & 1u) << (7 - (bit & 7)));
    }
}

void c2fx_frame_pack(const c2fx_indices *idx, uint8_t out[8])
{
    for (int j = 0; j < 8; j++) {
        out[j] = 0;
    }
    put_field(out, 0, 1, (unsigned)idx->v_a);
    put_field(out, 1, 1, (unsigned)idx->v_b);
    put_field(out, 2, 7, (unsigned)idx->i);
    put_field(out, 9, 5, (unsigned)idx->e);
    for (int j = 0; j < C2FX_LSP_ORDER; j++) {
        put_field(out, 14 + 5 * j, 5, (unsigned)idx->d[j]);
    }
}
#endif

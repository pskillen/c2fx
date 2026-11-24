// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The c2fx Authors

#include "c2m_frame.h"

unsigned c2m_gray_encode(unsigned n)
{
    return n ^ (n >> 1);
}

unsigned c2m_gray_decode(unsigned gray)
{
    unsigned n = gray;

    for (unsigned shifted = gray >> 1; shifted != 0u; shifted >>= 1) {
        n ^= shifted;
    }
    return n;
}

static int in_range(int value, int max_inclusive)
{
    return value >= 0 && value <= max_inclusive;
}

/* Bit 0 of the frame is bit 63 of the word (MSB of byte 0). */
static void put_field(uint64_t *word, int *bit, unsigned value, int width)
{
    for (int shift = width - 1; shift >= 0; shift--) {
        if (((value >> shift) & 1u) != 0u) {
            *word |= (uint64_t)1 << (63 - *bit);
        }
        *bit += 1;
    }
}

static unsigned get_field(uint64_t word, int bit, int width)
{
    unsigned value = 0;

    for (int i = 0; i < width; i++) {
        unsigned bit_val = (unsigned)((word >> (63 - (bit + i))) & 1u);
        value = (value << 1) | bit_val;
    }
    return value;
}

static uint64_t load_frame(const uint8_t in[C2M_FRAME_BYTES])
{
    uint64_t word = 0;

    for (int i = 0; i < C2M_FRAME_BYTES; i++) {
        word = (word << 8) | in[i];
    }
    return word;
}

static void store_frame(uint64_t word, uint8_t out[C2M_FRAME_BYTES])
{
    for (int i = 0; i < C2M_FRAME_BYTES; i++) {
        out[i] = (uint8_t)((word >> (56 - 8 * i)) & 0xffu);
    }
}

int c2m_frame_pack(const c2m_indices *idx, uint8_t out[C2M_FRAME_BYTES])
{
    uint64_t word = 0;
    int bit = 0;

    if (!in_range(idx->v_a, 1) || !in_range(idx->v_b, 1) || !in_range(idx->i, 127) ||
        !in_range(idx->e, 31)) {
        return -1;
    }
    for (int j = 0; j < C2M_LSP_ORDER; j++) {
        if (!in_range(idx->d[j], 31)) {
            return -1;
        }
    }

    put_field(&word, &bit, (unsigned)idx->v_a, 1);
    put_field(&word, &bit, (unsigned)idx->v_b, 1);
    put_field(&word, &bit, c2m_gray_encode((unsigned)idx->i), 7);
    put_field(&word, &bit, c2m_gray_encode((unsigned)idx->e), 5);
    for (int j = 0; j < C2M_LSP_ORDER; j++) {
        put_field(&word, &bit, c2m_gray_encode((unsigned)idx->d[j]), 5);
    }
    store_frame(word, out);
    return 0;
}

int c2m_frame_unpack(const uint8_t in[C2M_FRAME_BYTES], c2m_indices *idx)
{
    uint64_t word = load_frame(in);
    int bit = 0;

    idx->v_a = (int)get_field(word, bit, 1);
    bit += 1;
    idx->v_b = (int)get_field(word, bit, 1);
    bit += 1;
    idx->i = (int)c2m_gray_decode(get_field(word, bit, 7));
    bit += 7;
    idx->e = (int)c2m_gray_decode(get_field(word, bit, 5));
    bit += 5;
    for (int j = 0; j < C2M_LSP_ORDER; j++) {
        idx->d[j] = (int)c2m_gray_decode(get_field(word, bit, 5));
        bit += 5;
    }
    return 0;
}

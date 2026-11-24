// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The c2fx Authors

#ifndef C2M_FRAME_H
#define C2M_FRAME_H

#include <stdint.h>

/* Host float model. Not part of the fixed-point library. */

#define C2M_FRAME_BYTES 8
#define C2M_LSP_ORDER 10

/* One unpacked 3200 codec frame. Indices are Gray-decoded. */
typedef struct c2m_indices {
    int v_a; /* voicing bit 0, sub-frame a: 1 = voiced */
    int v_b; /* voicing bit 1, sub-frame b: 1 = voiced */
    int i;   /* w0 index, 0..127 */
    int e;   /* energy index, 0..31 */
    int d[C2M_LSP_ORDER]; /* LSP delta indices, 0..31, d[0] is d1 */
} c2m_indices;

/* Binary-reflected Gray code. gray = n ^ (n >> 1). */
unsigned c2m_gray_encode(unsigned n);
unsigned c2m_gray_decode(unsigned gray);

/* Pack bit 0 as the MSB of byte 0. Returns 0, or -1 if an index is out of range. */
int c2m_frame_pack(const c2m_indices *idx, uint8_t out[C2M_FRAME_BYTES]);

/* Every 8-byte pattern is a frame. Always returns 0. */
int c2m_frame_unpack(const uint8_t in[C2M_FRAME_BYTES], c2m_indices *idx);

#endif

// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The c2fx Authors

#ifndef C2FX_H
#define C2FX_H

#include "c2fx_types.h"

#define C2FX_VERSION_MAJOR 0
#define C2FX_VERSION_MINOR 0
#define C2FX_VERSION_PATCH 0

/* One frame of PCM at 8 kHz. */
#define C2FX_FRAME_LEN 160

/* One codec frame, 64 bits, packed into 8 bytes. */
#define C2FX_BITS_BYTES 8

/* Object sizes in bytes, as determined by sizeof on the target. */
#define C2FX_DEC_BYTES (sizeof(c2fx_dec_t))
#define C2FX_SCRATCH_BYTES (sizeof(c2fx_scratch_t))

const char *c2fx_version(void);

void c2fx_dec_init(c2fx_dec_t *d);

/* On C-SKY builds that enable C2FX_CSKY_HILO, compile the HI/LO
 * multiply bodies only into c2fx's own files with -Wa,-mdsp.
 * The RTOS does not save HI and LO across a context switch. Call
 * c2fx_encode and c2fx_decode from one task only. Nothing else that
 * can preempt that task or be preempted by it may use HI/LO.
 */

/* bits == NULL means the frame was lost. pcm is one 20 ms frame.
 * scratch may be reused for the next call.
 */
void c2fx_decode(
    c2fx_dec_t *d,
    const uint8_t *bits,
    int16_t pcm[C2FX_FRAME_LEN],
    c2fx_scratch_t *scratch);

#endif

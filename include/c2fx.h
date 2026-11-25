// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The c2fx Authors

#ifndef C2FX_H
#define C2FX_H

#include <stdint.h>

#define C2FX_VERSION_MAJOR 0
#define C2FX_VERSION_MINOR 0
#define C2FX_VERSION_PATCH 0

/* One frame of PCM at 8 kHz. */
#define C2FX_FRAME_LEN 160

/* One codec frame, 64 bits, packed into 8 bytes. */
#define C2FX_BITS_BYTES 8

/* Bytes of decoder state, rounded up to a multiple of 8. This holds what
 * has to survive from one call to the next: the previous sub-frame, the
 * excitation phase, the noise generator and the overlap-add tail.
 * c2fx_dec_t is that storage; the library casts it to its real struct.
 */
#define C2FX_DEC_BYTES 416

/* Bytes of per-call scratch, 8-byte aligned. One block for encode and
 * decode. It holds the FFT buffers, the envelope spectra and the
 * sub-frames for this call only; the next call may reuse the same memory.
 */
#define C2FX_SCRATCH_BYTES 7576

typedef struct c2fx_dec {
    uint64_t opaque[C2FX_DEC_BYTES / 8];
} c2fx_dec_t;

const char *c2fx_version(void);

void c2fx_dec_init(c2fx_dec_t *d);

/* On C-SKY builds that enable C2FX_CSKY_HILO, compile the HI/LO
 * multiply bodies only into c2fx's own files with -Wa,-mdsp.
 * The RTOS does not save HI and LO across a context switch. Call
 * c2fx_encode and c2fx_decode from one task only. Nothing else that
 * can preempt that task or be preempted by it may use HI/LO.
 */

/* bits == NULL means the frame was lost. pcm is one 20 ms frame.
 * scratch has C2FX_SCRATCH_BYTES bytes and is 8-byte aligned.
 */
void c2fx_decode(
    c2fx_dec_t *d,
    const uint8_t *bits,
    int16_t pcm[C2FX_FRAME_LEN],
    void *scratch);

#endif

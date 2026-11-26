// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The c2fx Authors

#ifndef C2FX_H
#define C2FX_H

#include "c2fx_types.h"

/* One frame of PCM at 8 kHz. */
#define C2FX_FRAME_LEN 160

/* One codec frame, 64 bits, packed into 8 bytes. */
#define C2FX_BITS_BYTES 8

/* Object sizes in bytes, as determined by sizeof on the target. */
#define C2FX_DEC_BYTES (sizeof(c2fx_dec_t))
#define C2FX_SCRATCH_BYTES (sizeof(c2fx_scratch_t))

#ifndef C2FX_NO_ENCODER
#define C2FX_ENC_BYTES (sizeof(c2fx_enc_t))

void c2fx_enc_init(c2fx_enc_t *e);

/* Each call consumes 160 samples and produces one packed frame. The
 * encoder delays output by 160 samples (one frame) for voicing lookahead.
 * Input before the start is zero. The first call returns an encoded silence
 * frame. After the last PCM frame, pass one zero-filled frame to emit the
 * last delayed frame. Scratch is clobbered on each call and may be shared
 * by encoder and decoder calls made from one task.
 */
void c2fx_encode(
    c2fx_enc_t *e,
    const int16_t pcm[C2FX_FRAME_LEN],
    uint8_t bits[C2FX_BITS_BYTES],
    c2fx_scratch_t *scratch);
#endif

/* When compiling c2fx_version.c, a consumer may pass -DC2FX_VERSION="\"x.y.z\"".
 * The default is "local".
 */
const char *c2fx_version(void);

void c2fx_dec_init(c2fx_dec_t *d);

/* On C-SKY builds that enable C2FX_CSKY_HILO, compile the HI/LO
 * multiply bodies only into c2fx's own files with -Wa,-mdsp.
 * The RTOS does not save HI and LO across a context switch. Call
 * c2fx_encode and c2fx_decode from one task only. Nothing else that
 * can preempt that task or be preempted by it may use HI/LO.
 */

/* bits == NULL means the frame was lost. pcm is one 20 ms frame.
 * Scratch is clobbered on each call and may be shared by encoder and
 * decoder calls made from one task.
 */
void c2fx_decode(
    c2fx_dec_t *d,
    const uint8_t *bits,
    int16_t pcm[C2FX_FRAME_LEN],
    c2fx_scratch_t *scratch);

#endif

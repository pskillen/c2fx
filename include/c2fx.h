// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The c2fx Authors

#ifndef C2FX_H
#define C2FX_H

#define C2FX_VERSION_MAJOR 0
#define C2FX_VERSION_MINOR 0
#define C2FX_VERSION_PATCH 0

/* One frame of PCM at 8 kHz. */
#define C2FX_FRAME_LEN 160

/* One codec frame, 64 bits, packed into 8 bytes. */
#define C2FX_BITS_BYTES 8

/* The encode and decode API lands with the decoder. */
const char *c2fx_version(void);

#endif

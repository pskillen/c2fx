// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The c2fx Authors

#ifndef C2_HEADER_H
#define C2_HEADER_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Skip the optional 7-byte header for mode 3200 (spec 2.3). */
static inline int c2fx_skip_header(FILE *in)
{
    static const uint8_t header[7] = {0xc0, 0xde, 0xc2, 0x01, 0x02, 0x00, 0x00};
    uint8_t probe[sizeof header];
    size_t n = fread(probe, 1, sizeof probe, in);

    if (n == sizeof probe && memcmp(probe, header, sizeof probe) == 0) {
        return 0;
    }
    return fseek(in, 0, SEEK_SET);
}

#endif

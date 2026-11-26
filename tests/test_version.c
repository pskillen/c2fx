// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The c2fx Authors

#include "c2fx.h"

#include <stdio.h>
#include <string.h>

static int is_digit(char ch)
{
    return ch >= '0' && ch <= '9';
}

static int read_number(const char **version)
{
    const char *p = *version;

    if (!is_digit(*p) || (*p == '0' && is_digit(p[1]))) {
        return 0;
    }
    do {
        p++;
    } while (is_digit(*p));
    *version = p;
    return 1;
}

static int valid_version(const char *version)
{
    size_t hex = strspn(version, "0123456789abcdefABCDEF");

    if (strcmp(version, "local") == 0 ||
        (hex >= 7 && hex <= 12 && version[hex] == '\0')) {
        return 1;
    }
    if (!read_number(&version) || *version++ != '.' ||
        !read_number(&version) || *version++ != '.' ||
        !read_number(&version)) {
        return 0;
    }
    if (*version == '\0') {
        return 1;
    }
    if (strncmp(version, "-rc.", 4) != 0) {
        return 0;
    }
    version += 4;
    return read_number(&version) && *version == '\0';
}

int main(void)
{
    const char *version = c2fx_version();

    if (!valid_version(version)) {
        fprintf(stderr, "version is \"%s\", expected local, a short Git SHA, or X.Y.Z[-rc.N]\n", version);
        return 1;
    }
    if (C2FX_SCRATCH_BYTES > 6144 || C2FX_DEC_BYTES > 1024) {
        fprintf(stderr, "decoder scratch %zu bytes (max 6144), state %zu bytes (max 1024)\n",
                C2FX_SCRATCH_BYTES, C2FX_DEC_BYTES);
        return 1;
    }
#ifndef C2FX_NO_ENCODER
    if (C2FX_ENC_BYTES > 1536) {
        fprintf(stderr, "encoder state %zu bytes (max 1536)\n", C2FX_ENC_BYTES);
        return 1;
    }
#endif
    if (C2FX_FRAME_LEN != 160) {
        fprintf(stderr, "C2FX_FRAME_LEN is %d, expected 160\n", C2FX_FRAME_LEN);
        return 1;
    }
    if (C2FX_BITS_BYTES != 8) {
        fprintf(stderr, "C2FX_BITS_BYTES is %d, expected 8\n", C2FX_BITS_BYTES);
        return 1;
    }
    printf("c2fx version: %s\n", version);
    return 0;
}

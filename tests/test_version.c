// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The c2fx Authors

#include "c2fx.h"

#include <stdio.h>
#include <string.h>

int main(void)
{
    if (strcmp(c2fx_version(), "0.0.0") != 0) {
        fprintf(stderr, "version is \"%s\", expected 0.0.0\n", c2fx_version());
        return 1;
    }
    if (C2FX_FRAME_LEN != 160) {
        fprintf(stderr, "C2FX_FRAME_LEN is %d, expected 160\n", C2FX_FRAME_LEN);
        return 1;
    }
    if (C2FX_BITS_BYTES != 8) {
        fprintf(stderr, "C2FX_BITS_BYTES is %d, expected 8\n", C2FX_BITS_BYTES);
        return 1;
    }
    return 0;
}

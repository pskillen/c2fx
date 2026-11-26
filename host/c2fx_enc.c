// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The c2fx Authors

#include "c2fx.h"

#include <stdio.h>

static int encode_file(FILE *in, FILE *out)
{
    c2fx_enc_t state;
    c2fx_scratch_t scratch;
    int started = 0;
    uint8_t raw[2 * C2FX_FRAME_LEN];
    int16_t pcm[C2FX_FRAME_LEN];
    uint8_t bits[C2FX_BITS_BYTES];
    size_t got;

    c2fx_enc_init(&state);
    while ((got = fread(raw, 1, sizeof raw, in)) == sizeof raw) {
        for (int j = 0; j < C2FX_FRAME_LEN; j++) {
            int32_t u = raw[2 * j] + 256 * raw[2 * j + 1];

            pcm[j] = (int16_t)(u > INT16_MAX ? u - 65536 : u);
        }
        c2fx_encode(&state, pcm, bits, &scratch);
        if (started && fwrite(bits, 1, sizeof bits, out) != sizeof bits) {
            return -1;
        }
        started = 1;
    }
    if (ferror(in) || got != 0 || !started) {
        return -1;
    }
    for (int j = 0; j < C2FX_FRAME_LEN; j++) {
        pcm[j] = 0;
    }
    c2fx_encode(&state, pcm, bits, &scratch);
    return fwrite(bits, 1, sizeof bits, out) == sizeof bits ? 0 : -1;
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: c2fx_enc <in.pcm> <out.c2>\n");
        return 2;
    }
    FILE *in = fopen(argv[1], "rb");
    if (in == NULL) {
        fprintf(stderr, "cannot open %s\n", argv[1]);
        return 1;
    }
    FILE *out = fopen(argv[2], "wb");
    if (out == NULL) {
        fprintf(stderr, "cannot open %s\n", argv[2]);
        fclose(in);
        return 1;
    }
    int status = encode_file(in, out);
    fclose(in);
    if (fclose(out) != 0 || status != 0) {
        fprintf(stderr, "cannot encode %s to %s (input must contain whole 160-sample frames)\n",
                argv[1], argv[2]);
        return 1;
    }
    return 0;
}

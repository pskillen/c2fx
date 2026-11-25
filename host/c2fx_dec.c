// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The c2fx Authors
// Decode one .c2 bitstream to int16 little-endian PCM. A 7-byte header is
// skipped when the file name ends in .c2 and the mode byte is 0x00.

#include "c2_header.h"
#include "c2fx.h"

static int write_frame(FILE *out, const int16_t *pcm)
{
    for (int i = 0; i < C2FX_FRAME_LEN; i++) {
        uint8_t bytes[2];
        uint16_t sample = (uint16_t)pcm[i];

        bytes[0] = (uint8_t)(sample & 0xffu);
        bytes[1] = (uint8_t)((sample >> 8) & 0xffu);
        if (fwrite(bytes, 1, 2, out) != 2) {
            return -1;
        }
    }
    return 0;
}

int main(int argc, char **argv)
{
    FILE *in;
    FILE *out;
    c2fx_dec_t dec;
    uint64_t scratch[C2FX_SCRATCH_BYTES / 8];
    int frames = 0;

    if (argc != 3) {
        fprintf(stderr, "usage: c2fx_dec <in.c2> <out.pcm>\n");
        return 2;
    }
    in = fopen(argv[1], "rb");
    if (in == NULL) {
        fprintf(stderr, "cannot open %s\n", argv[1]);
        return 1;
    }
    out = fopen(argv[2], "wb");
    if (out == NULL) {
        fprintf(stderr, "cannot open %s\n", argv[2]);
        fclose(in);
        return 1;
    }
    if (c2fx_skip_header(in) != 0) {
        fprintf(stderr, "cannot rewind %s\n", argv[1]);
        fclose(in);
        fclose(out);
        return 1;
    }
    c2fx_dec_init(&dec);
    for (;;) {
        uint8_t raw[C2FX_BITS_BYTES];
        int16_t pcm[C2FX_FRAME_LEN];
        size_t got = fread(raw, 1, C2FX_BITS_BYTES, in);

        if (got == 0) {
            break;
        }
        if (got != C2FX_BITS_BYTES) {
            fprintf(stderr, "truncated frame %d\n", frames);
            fclose(in);
            fclose(out);
            return 1;
        }
        c2fx_decode(&dec, raw, pcm, scratch);
        if (write_frame(out, pcm) != 0) {
            fprintf(stderr, "cannot write %s\n", argv[2]);
            fclose(in);
            fclose(out);
            return 1;
        }
        frames += 1;
    }
    fclose(in);
    if (fclose(out) != 0) {
        fprintf(stderr, "cannot close %s\n", argv[2]);
        return 1;
    }
    return 0;
}

// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The c2fx Authors

#include "c2m_encode.h"
#include "c2m_synth.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static int16_t *read_pcm(const char *path, int *n_out)
{
    FILE *in = fopen(path, "rb");
    long bytes;
    uint8_t *raw;
    int16_t *pcm;
    int n;

    if (in == NULL) {
        fprintf(stderr, "cannot open %s\n", path);
        return NULL;
    }
    if (fseek(in, 0, SEEK_END) != 0) {
        fprintf(stderr, "cannot seek %s\n", path);
        fclose(in);
        return NULL;
    }
    bytes = ftell(in);
    if (bytes < 0 || fseek(in, 0, SEEK_SET) != 0) {
        fprintf(stderr, "cannot seek %s\n", path);
        fclose(in);
        return NULL;
    }
    if ((bytes % 2) != 0) {
        fprintf(stderr, "%s has an odd length\n", path);
        fclose(in);
        return NULL;
    }
    n = (int)(bytes / 2);
    raw = malloc((size_t)bytes);
    pcm = malloc((size_t)n * sizeof pcm[0]);
    if (raw == NULL || pcm == NULL) {
        fprintf(stderr, "out of memory\n");
        free(raw);
        free(pcm);
        fclose(in);
        return NULL;
    }
    if (bytes > 0 && fread(raw, 1, (size_t)bytes, in) != (size_t)bytes) {
        fprintf(stderr, "cannot read %s\n", path);
        free(raw);
        free(pcm);
        fclose(in);
        return NULL;
    }
    fclose(in);
    for (int i = 0; i < n; i++) {
        uint16_t u = (uint16_t)raw[2 * i] | ((uint16_t)raw[2 * i + 1] << 8);

        pcm[i] = (int16_t)u;
    }
    free(raw);
    *n_out = n;
    return pcm;
}

int main(int argc, char **argv)
{
    int16_t *pcm;
    uint8_t *bits;
    int n;
    int frames;
    FILE *out;

    if (argc != 3) {
        fprintf(stderr, "usage: c2m_enc <in.pcm> <out.c2>\n");
        return 2;
    }
    pcm = read_pcm(argv[1], &n);
    if (pcm == NULL) {
        return 1;
    }
    if (n == 0 || (n % C2M_FRAME_SAMPLES) != 0) {
        fprintf(stderr, "%s is not a whole number of 160-sample frames\n", argv[1]);
        free(pcm);
        return 1;
    }
    frames = n / C2M_FRAME_SAMPLES;
    bits = malloc((size_t)frames * C2M_FRAME_BYTES);
    if (bits == NULL) {
        fprintf(stderr, "out of memory\n");
        free(pcm);
        return 1;
    }
    if (c2m_encode(pcm, n, bits) != frames) {
        fprintf(stderr, "encode failed\n");
        free(bits);
        free(pcm);
        return 1;
    }
    out = fopen(argv[2], "wb");
    if (out == NULL) {
        fprintf(stderr, "cannot open %s\n", argv[2]);
        free(bits);
        free(pcm);
        return 1;
    }
    if (fwrite(bits, 1, (size_t)frames * C2M_FRAME_BYTES, out) != (size_t)frames * C2M_FRAME_BYTES) {
        fprintf(stderr, "cannot write %s\n", argv[2]);
        fclose(out);
        free(bits);
        free(pcm);
        return 1;
    }
    if (fclose(out) != 0) {
        fprintf(stderr, "cannot close %s\n", argv[2]);
        free(bits);
        free(pcm);
        return 1;
    }
    free(bits);
    free(pcm);
    return 0;
}

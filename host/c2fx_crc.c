// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The c2fx Authors

#include "c2_header.h"
#include "c2fx.h"

#include <inttypes.h>

static uint32_t crc32_byte(uint32_t crc, uint8_t byte)
{
    crc ^= byte;
    for (int bit = 0; bit < 8; bit++) {
        uint32_t mask = 0u - (crc & 1u);

        crc = (crc >> 1) ^ (UINT32_C(0xedb88320) & mask);
    }
    return crc;
}

static int self_test(void)
{
    static const uint8_t input[] = "123456789";
    uint32_t crc = UINT32_MAX;

    for (size_t i = 0; i < sizeof input - 1; i++) {
        crc = crc32_byte(crc, input[i]);
    }
    crc ^= UINT32_MAX;
    if (crc != UINT32_C(0xcbf43926)) {
        fprintf(stderr, "CRC-32 of 123456789: %08" PRIx32 ", expected cbf43926\n", crc);
        return 1;
    }
    puts("CRC-32 of 123456789: cbf43926");
    return 0;
}

static int crc_file(const char *path)
{
    FILE *in = fopen(path, "rb");
    const char *slash = strrchr(path, '/');
    const char *name = slash == NULL ? path : slash + 1;
    c2fx_dec_t dec;
    uint64_t scratch[C2FX_SCRATCH_BYTES / 8];
    uint32_t crc = UINT32_MAX;
    size_t frames = 0;
    uint8_t raw[C2FX_BITS_BYTES];
    size_t got;
    int read_error;

    if (in == NULL) {
        fprintf(stderr, "cannot open %s\n", path);
        return 1;
    }
    if (c2fx_skip_header(in) != 0) {
        fprintf(stderr, "cannot rewind %s\n", path);
        fclose(in);
        return 1;
    }
    c2fx_dec_init(&dec);
    while ((got = fread(raw, 1, sizeof raw, in)) == sizeof raw) {
        int16_t pcm[C2FX_FRAME_LEN];

        c2fx_decode(&dec, raw, pcm, scratch);
        for (int i = 0; i < C2FX_FRAME_LEN; i++) {
            uint16_t sample = (uint16_t)pcm[i];

            crc = crc32_byte(crc, (uint8_t)sample);
            crc = crc32_byte(crc, (uint8_t)(sample >> 8));
        }
        frames++;
    }
    read_error = ferror(in);
    fclose(in);
    if (read_error) {
        fprintf(stderr, "cannot read %s\n", path);
        return 1;
    }
    if (got != 0) {
        fprintf(stderr, "%s: truncated frame %zu\n", path, frames);
        return 1;
    }
    return printf("%s %zu %08" PRIx32 "\n", name, frames, crc ^ UINT32_MAX) < 0;
}

int main(int argc, char **argv)
{
    int failed = 0;

    if (argc == 2 && strcmp(argv[1], "--self-test") == 0) {
        return self_test();
    }
    if (argc < 2) {
        fprintf(stderr, "usage: c2fx_crc <file.c2>...\n");
        return 2;
    }
    for (int i = 1; i < argc; i++) {
        failed |= crc_file(argv[i]);
    }
    if (fflush(stdout) != 0) {
        fprintf(stderr, "cannot write CRC output\n");
        return 1;
    }
    return failed;
}

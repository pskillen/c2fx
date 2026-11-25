// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The c2fx Authors
// D4. Random frames, table extremes, and a lost-frame run. The decoder must
// stay inside int16, keep producing samples after a good frame, and follow
// the model's mute. A lost run decays, then is exactly zero from the frame
// the model goes silent.

#include "c2fx.h"
#include "c2m_frame.h"
#include "c2m_synth.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#ifndef C2FX_ROBUST_FRAMES
#define C2FX_ROBUST_FRAMES 20000
#endif

static uint32_t xorshift(uint32_t *s)
{
    uint32_t x = *s;

    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *s = x;
    return x;
}

static int in_range(const int16_t *pcm)
{
    for (int i = 0; i < C2FX_FRAME_LEN; i++) {
        int32_t s = pcm[i];

        if (s < -32768 || s > 32767) {
            return 0;
        }
    }
    return 1;
}

static int any_nonzero(const int16_t *pcm)
{
    for (int i = 0; i < C2FX_FRAME_LEN; i++) {
        if (pcm[i] != 0) {
            return 1;
        }
    }
    return 0;
}

static int all_zero(const int16_t *pcm)
{
    return !any_nonzero(pcm);
}

static double rms(const int16_t *pcm)
{
    double s = 0.0;

    for (int i = 0; i < C2FX_FRAME_LEN; i++) {
        s += (double)pcm[i] * (double)pcm[i];
    }
    return sqrt(s / (double)C2FX_FRAME_LEN);
}

static int decode_bytes(c2fx_dec_t *dec, const uint8_t *bits, c2fx_scratch_t *scratch, int16_t *pcm)
{
    c2fx_decode(dec, bits, pcm, scratch);
    if (!in_range(pcm)) {
        fprintf(stderr, "sample outside int16\n");
        return 1;
    }
    return 0;
}

/* A known loud voiced frame, so a stuck decoder fails this check. */
static int not_stuck(c2fx_dec_t *dec, c2fx_scratch_t *scratch, const uint8_t *good)
{
    int heard = 0;

    for (int n = 0; n < 2; n++) {
        int16_t pcm[C2FX_FRAME_LEN];

        if (decode_bytes(dec, good, scratch, pcm) != 0) {
            return 1;
        }
        if (any_nonzero(pcm)) {
            heard = 1;
        }
    }
    if (!heard) {
        fprintf(stderr, "decoder stayed silent after a good frame\n");
        return 1;
    }
    return 0;
}

static int random_frames(c2fx_dec_t *dec, c2fx_scratch_t *scratch, const uint8_t *good)
{
    uint32_t state = 0xC2F0A5A5u;

    for (int n = 0; n < C2FX_ROBUST_FRAMES; n++) {
        uint8_t raw[C2FX_BITS_BYTES];
        int16_t pcm[C2FX_FRAME_LEN];
        uint32_t a = xorshift(&state);
        uint32_t b = xorshift(&state);

        raw[0] = (uint8_t)(a >> 24);
        raw[1] = (uint8_t)(a >> 16);
        raw[2] = (uint8_t)(a >> 8);
        raw[3] = (uint8_t)a;
        raw[4] = (uint8_t)(b >> 24);
        raw[5] = (uint8_t)(b >> 16);
        raw[6] = (uint8_t)(b >> 8);
        raw[7] = (uint8_t)b;
        if (decode_bytes(dec, raw, scratch, pcm) != 0) {
            fprintf(stderr, "random frame %d\n", n);
            return 1;
        }
    }
    return not_stuck(dec, scratch, good);
}

static int feed_pattern(c2fx_dec_t *dec, c2fx_scratch_t *scratch, const uint8_t *raw, const uint8_t *good, const char *what)
{
    int16_t pcm[C2FX_FRAME_LEN];

    if (decode_bytes(dec, raw, scratch, pcm) != 0) {
        fprintf(stderr, "%s\n", what);
        return 1;
    }
    return not_stuck(dec, scratch, good);
}

static int extremes(c2fx_dec_t *dec, c2fx_scratch_t *scratch, const uint8_t *good)
{
    uint8_t raw[C2FX_BITS_BYTES];
    c2m_indices idx;
    static const int lsp4000[C2M_LSP_ORDER] = {31, 31, 31, 31, 7, 10, 10, 10, 10, 10};

    memset(raw, 0, sizeof raw);
    if (feed_pattern(dec, scratch, raw, good, "all-zero frame") != 0) {
        return 1;
    }
    memset(raw, 0xff, sizeof raw);
    if (feed_pattern(dec, scratch, raw, good, "all-ones frame") != 0) {
        return 1;
    }

    for (int j = 0; j < C2M_LSP_ORDER; j++) {
        for (int extreme = 0; extreme <= 31; extreme += 31) {
            memset(&idx, 0, sizeof idx);
            idx.v_a = 1;
            idx.v_b = 1;
            idx.i = 60;
            idx.e = 16;
            idx.d[j] = extreme;
            if (c2m_frame_pack(&idx, raw) != 0 || feed_pattern(dec, scratch, raw, good, "lsp extreme") != 0) {
                fprintf(stderr, "lsp %d = %d\n", j, extreme);
                return 1;
            }
        }
    }

    memset(&idx, 0, sizeof idx);
    idx.v_a = 1;
    idx.v_b = 1;
    idx.i = 60;
    idx.e = 20;
    for (int j = 0; j < C2M_LSP_ORDER; j++) {
        idx.d[j] = lsp4000[j];
    }
    if (c2m_frame_pack(&idx, raw) != 0 || feed_pattern(dec, scratch, raw, good, "4000 Hz LSP frame") != 0) {
        return 1;
    }

    for (int pitch = 0; pitch <= 127; pitch += 127) {
        memset(&idx, 0, sizeof idx);
        idx.v_a = 1;
        idx.v_b = 1;
        idx.i = pitch;
        idx.e = 16;
        if (c2m_frame_pack(&idx, raw) != 0 || feed_pattern(dec, scratch, raw, good, "w0 extreme") != 0) {
            fprintf(stderr, "w0 index %d\n", pitch);
            return 1;
        }
    }
    for (int energy = 0; energy <= 31; energy += 31) {
        memset(&idx, 0, sizeof idx);
        idx.v_a = 1;
        idx.v_b = 1;
        idx.i = 40;
        idx.e = energy;
        if (c2m_frame_pack(&idx, raw) != 0 || feed_pattern(dec, scratch, raw, good, "energy extreme") != 0) {
            fprintf(stderr, "energy index %d\n", energy);
            return 1;
        }
    }
    return 0;
}

/* Ten lost frames after a loud one. Output decays, then matches the model's zeros. */
static int lost_run(const uint8_t *good)
{
    c2fx_dec_t dec;
    c2m_decode md;
    c2fx_scratch_t scratch;
    c2m_indices idx;
    int16_t model[C2M_FRAME_SAMPLES];
    int16_t fx[C2FX_FRAME_LEN];
    double first = 0.0;
    double later = 0.0;
    int mute = -1;
    int heard = 0;

    c2fx_dec_init(&dec);
    c2m_decode_init(&md);
    c2m_frame_unpack(good, &idx);
    c2m_decode_pcm(&md, &idx, model);
    if (decode_bytes(&dec, good, &scratch, fx) != 0) {
        return 1;
    }
    for (int n = 0; n < 10; n++) {
        c2m_decode_pcm(&md, NULL, model);
        c2fx_decode(&dec, NULL, fx, &scratch);
        if (!in_range(fx)) {
            fprintf(stderr, "lost frame %d outside int16\n", n);
            return 1;
        }
        if (all_zero(model)) {
            if (mute < 0) {
                mute = n;
            }
            if (!all_zero(fx)) {
                fprintf(stderr, "lost frame %d is not silent where the model is\n", n);
                return 1;
            }
        } else if (all_zero(fx)) {
            fprintf(stderr, "lost frame %d is silent where the model is not\n", n);
            return 1;
        }
        if (n == 0) {
            first = rms(fx);
        }
        if (n == 3) {
            later = rms(fx);
        }
    }
    if (mute < 0) {
        fprintf(stderr, "model did not mute within 10 lost frames\n");
        return 1;
    }
    if (!(first > 0.0) || !(later < 0.6 * first)) {
        fprintf(stderr, "lost-frame energy did not decay (%.1f to %.1f)\n", first, later);
        return 1;
    }
    for (int n = 0; n < 2; n++) {
        c2m_decode_pcm(&md, &idx, model);
        if (decode_bytes(&dec, good, &scratch, fx) != 0) {
            return 1;
        }
        if (any_nonzero(fx)) {
            heard = 1;
        }
    }
    if (!heard) {
        fprintf(stderr, "no audio within 2 frames after the mute\n");
        return 1;
    }
    printf("lost run: mute at frame %d, rms %.1f -> %.1f\n", mute, first, later);
    return 0;
}

static int load_good(uint8_t raw[C2FX_BITS_BYTES])
{
    FILE *fp = fopen("vectors/sine_200hz.c2", "rb");
    size_t n;

    if (fp == NULL) {
        fprintf(stderr, "cannot open vectors/sine_200hz.c2\n");
        return 1;
    }
    n = fread(raw, 1, C2FX_BITS_BYTES, fp);
    fclose(fp);
    if (n != C2FX_BITS_BYTES) {
        fprintf(stderr, "sine_200hz.c2 is short\n");
        return 1;
    }
    return 0;
}

int main(void)
{
    c2fx_dec_t dec;
    c2fx_scratch_t scratch;
    uint8_t good[C2FX_BITS_BYTES];

    if (load_good(good) != 0) {
        return 1;
    }
    if (lost_run(good) != 0) {
        return 1;
    }
    c2fx_dec_init(&dec);
    if (extremes(&dec, &scratch, good) != 0) {
        return 1;
    }
    printf("extremes: all-zero, all-ones, LSP, w0, energy\n");
    c2fx_dec_init(&dec);
    if (random_frames(&dec, &scratch, good) != 0) {
        return 1;
    }
    printf("random frames: %d, output in int16, recovered after a good frame\n", C2FX_ROBUST_FRAMES);
    return 0;
}

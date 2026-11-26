// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The c2fx Authors

#include "c2fx.h"
#include "c2fx_internal.h"
#include "c2m_encode.h"
#include "c2m_quant.h"

#include <glob.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int16_t *read_pcm(const char *path, int *count)
{
    FILE *file = fopen(path, "rb");
    int16_t *pcm = NULL;
    long bytes;

    if (file == NULL) {
        return NULL;
    }
    if (fseek(file, 0, SEEK_END) != 0 || (bytes = ftell(file)) <= 0 ||
        bytes > INT_MAX || bytes % (2 * C2FX_FRAME_LEN) != 0 ||
        fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    *count = (int)(bytes / 2);
    pcm = malloc((size_t)*count * sizeof *pcm);
    if (pcm != NULL) {
        for (int i = 0; i < *count; i++) {
            int lo = fgetc(file);
            int hi = fgetc(file);
            int sample;

            if (lo == EOF || hi == EOF) {
                free(pcm);
                pcm = NULL;
                break;
            }
            sample = hi * 256 + lo;
            pcm[i] = (int16_t)(sample > INT16_MAX ? sample - 65536 : sample);
        }
    }
    fclose(file);
    return pcm;
}

static int max_difference(int previous, int actual, int expected)
{
    int diff = abs(actual - expected);
    return diff > previous ? diff : previous;
}

static void compare_indices(const uint8_t *actual, const uint8_t *expected,
                            c2fx_indices *worst)
{
    c2fx_indices a;
    c2m_indices b;

    c2fx_frame_unpack(actual, &a);
    c2m_frame_unpack(expected, &b);
    worst->v_a = max_difference(worst->v_a, a.v_a, b.v_a);
    worst->v_b = max_difference(worst->v_b, a.v_b, b.v_b);
    worst->i = max_difference(worst->i, a.i, b.i);
    worst->e = max_difference(worst->e, a.e, b.e);
    for (int j = 0; j < C2FX_LSP_ORDER; j++) {
        worst->d[j] = max_difference(worst->d[j], a.d[j], b.d[j]);
    }
}

static int check_resume(const char *path, const int16_t *pcm, int frames,
                        const uint8_t *original, c2fx_enc_t *state)
{
    c2fx_enc_t resumed;
    c2fx_scratch_t scratch;
    const int16_t zero[C2FX_FRAME_LEN] = {0};
    const int split = frames / 2 + 1;

    c2fx_enc_init(state);
    for (int j = 0; j <= frames; j++) {
        uint8_t bits[C2FX_BITS_BYTES];
        if (j == split) {
            memcpy(&resumed, state, sizeof resumed);
            memset(state, 0, sizeof *state);
            state = &resumed;
            memset(&scratch, 0, sizeof scratch);
        }
        c2fx_encode(state, j < frames ? pcm + j * C2FX_FRAME_LEN : zero, bits, &scratch);
        if (memcmp(bits, original + (size_t)j * C2FX_BITS_BYTES, C2FX_BITS_BYTES) != 0) {
            fprintf(stderr, "%s: resumed or reinitialized stream differs on call %d\n", path, j);
            return 1;
        }
    }
    return 0;
}

static int compare_stream(const char *path, const int16_t *pcm, int n)
{
    c2fx_enc_t state;
    c2fx_scratch_t scratch;
    c2fx_indices worst = {0};
    const int16_t zero[C2FX_FRAME_LEN] = {0};
    int identical = 0;

    int frames = n / C2FX_FRAME_LEN;
    uint8_t *expected = malloc((size_t)frames * C2FX_BITS_BYTES);
    uint8_t *original = malloc((size_t)(frames + 1) * C2FX_BITS_BYTES);
    if (expected == NULL || original == NULL || c2m_encode(pcm, n, expected) != frames) {
        fprintf(stderr, "%s: model encode or allocation failed\n", path);
        free(original);
        free(expected);
        return 1;
    }
    c2fx_enc_init(&state);
    for (int j = 0; j <= frames; j++) {
        uint8_t *actual = original + (size_t)j * C2FX_BITS_BYTES;
        c2fx_encode(&state, j < frames ? pcm + j * C2FX_FRAME_LEN : zero, actual, &scratch);
        if (j > 0) {
            const uint8_t *ref = expected + (j - 1) * C2FX_BITS_BYTES;
            identical += memcmp(actual, ref, C2FX_BITS_BYTES) == 0;
            compare_indices(actual, ref, &worst);
        }
    }
    printf("%s: %d frames, %.4f%% identical; worst w0 %d energy %d LSPs",
           path, frames, 100.0 * identical / frames, worst.i, worst.e);
    int failed = 100LL * identical < 98LL * frames || worst.i > 1 || worst.e > 1;
    for (int j = 0; j < C2FX_LSP_ORDER; j++) {
        printf(" %d", worst.d[j]);
        failed |= worst.d[j] > 1;
    }
    printf(" voicing %d %d\n", worst.v_a, worst.v_b);
    if (failed) {
        fprintf(stderr, "%s: E4 failed (%d/%d identical, require 98%% and at most one step per index)\n",
                path, identical, frames);
    }
    uint8_t silence[C2FX_BITS_BYTES];
    if (c2m_encode(zero, C2FX_FRAME_LEN, silence) != 1 ||
        memcmp(original, silence, sizeof silence) != 0) {
        fprintf(stderr, "%s: startup frame differs from encoded silence\n", path);
        failed = 1;
    }
    failed |= check_resume(path, pcm, frames, original, &state);
    free(original);
    free(expected);
    return failed;
}

static int run_file(const char *path)
{
    int n;
    int16_t *pcm = read_pcm(path, &n);
    if (pcm == NULL) {
        fprintf(stderr, "%s: cannot read whole PCM frames\n", path);
        return 1;
    }
    int failed = compare_stream(path, pcm, n);
    free(pcm);
    return failed;
}

static int indices_in_range(const c2fx_indices *idx)
{
    if (idx->v_a < 0 || idx->v_a > 1 || idx->v_b < 0 || idx->v_b > 1 ||
        idx->i < 0 || idx->i > 127 || idx->e < 0 || idx->e > 31) {
        return 0;
    }
    for (int j = 0; j < C2FX_LSP_ORDER; j++) {
        if (idx->d[j] < 0 || idx->d[j] > 31) {
            return 0;
        }
    }
    return 1;
}

static int check_edges(void)
{
    enum { SILENCE, SQUARE_50, SQUARE_400, SQUARE_3999, NOISE, IMPULSE, ALTERNATING };
    const char *names[] = {"silence", "square 50 Hz", "square 400 Hz", "square 3999 Hz",
                           "noise", "impulse 50 Hz", "alternating"};
    const int rates[] = {50, 400, 3999};
    uint32_t rng = UINT32_C(0xC2F32001);
    c2fx_enc_t state;
    c2fx_scratch_t scratch;
    int16_t pcm[C2FX_FRAME_LEN];

    for (int signal = SILENCE; signal <= ALTERNATING; signal++) {
        c2fx_enc_init(&state);
        for (int frame = 0; frame < 12; frame++) {
            uint8_t bits[C2FX_BITS_BYTES];
            c2fx_indices idx;
            for (int j = 0; j < C2FX_FRAME_LEN; j++) {
                int sample = frame * C2FX_FRAME_LEN + j;
                int value = 0;
                if (signal >= SQUARE_50 && signal <= SQUARE_3999) {
                    int rate = rates[signal - SQUARE_50];
                    value = (sample * 2 * rate / 8000) % 2 == 0 ? INT16_MIN : INT16_MAX;
                } else if (signal == NOISE) {
                    rng ^= rng << 13;
                    rng ^= rng >> 17;
                    rng ^= rng << 5;
                    value = (int)(rng >> 16);
                    value = value > INT16_MAX ? value - 65536 : value;
                } else if (signal == IMPULSE) {
                    value = j == 0 ? INT16_MAX : 0;
                } else if (signal == ALTERNATING) {
                    value = j % 2 == 0 ? INT16_MAX : -INT16_MAX;
                }
                pcm[j] = (int16_t)value;
            }
            c2fx_encode(&state, pcm, bits, &scratch);
            c2fx_frame_unpack(bits, &idx);
            /* Unpacking alone only proves that the fields fit on the wire.
             * Check the pending indices before the packer can truncate them.
             * Only v_b still needs the next row's decision.
             */
            c2fx_indices pending = state.pending;
            if (!indices_in_range(&idx) || !indices_in_range(&pending)) {
                fprintf(stderr, "%s: encoder frame %d has invalid indices\n", names[signal], frame);
                return 1;
            }
        }
    }
    return 0;
}

static int check_lookahead(void)
{
    int16_t pcm[3 * C2FX_FRAME_LEN];
    uint8_t expected[3 * C2FX_BITS_BYTES];
    uint8_t bits[C2FX_BITS_BYTES];
    c2fx_enc_t state;
    c2fx_scratch_t scratch;

    /* A high tone gives the row at center 160 bridge-band tilt and
     * unvoiced base. The low tone makes both adjacent rows voiced.
     */
    for (int j = 0; j < 3 * C2FX_FRAME_LEN; j++) {
        int rel = j - C2FX_FRAME_LEN;
        int high = abs(rel) <= 60;
        int amplitude = high ? 16000 : 4000;
        int hz = high ? 1200 : 200;

        pcm[j] = (int16_t)lround(amplitude * cos(2.0 * C2M_PI * hz * rel / 8000.0));
    }
    if (c2m_encode(pcm, 3 * C2FX_FRAME_LEN, expected) != 3 ||
        (expected[C2FX_BITS_BYTES] & 0x40u) == 0) {
        fprintf(stderr, "lookahead fixture: model did not bridge the row at center 160\n");
        return 1;
    }
    c2fx_enc_init(&state);
    for (int j = 0; j < 3; j++) {
        c2fx_encode(&state, pcm + j * C2FX_FRAME_LEN, bits, &scratch);
    }
    if ((bits[0] & 0x40u) == 0) {
        fprintf(stderr, "lookahead: encoder did not bridge the row at center 160\n");
        return 1;
    }
    return 0;
}

static int check_short_stream(void)
{
    int16_t pcm[C2FX_FRAME_LEN] = {0};

    /* Pad the half-frame of input to the API's 160-sample block size.
     * Its model-aligned frame is returned by the zero-padding call.
     */
    for (int j = 0; j < C2FX_HOP; j++) {
        pcm[j] = 8000;
    }
    return compare_stream("zero-padded 80-sample stream", pcm, C2FX_FRAME_LEN);
}

int main(void)
{
    glob_t paths = {0};
    int failed = 0;

    if (glob("tests/data/*/*.pcm", 0, NULL, &paths) != 0 || paths.gl_pathc != 14) {
        fprintf(stderr, "expected 14 test PCM files\n");
        globfree(&paths);
        return 1;
    }
    for (size_t j = 0; j < paths.gl_pathc; j++) {
        failed |= run_file(paths.gl_pathv[j]);
    }
    globfree(&paths);
    failed |= check_short_stream();
    failed |= check_lookahead();
    failed |= check_edges();
    return failed;
}

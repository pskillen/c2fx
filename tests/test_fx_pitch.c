// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The c2fx Authors

#include "c2fx_internal.h"
#include "c2m_analysis.h"
#include "c2m_pitch.h"
#include "c2m_voicing.h"

#include <glob.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static int16_t *read_pcm(const char *path, int *count)
{
    FILE *file = fopen(path, "rb");
    int16_t *pcm = NULL;
    long bytes;

    if (file == NULL) {
        return NULL;
    }
    if (fseek(file, 0, SEEK_END) != 0 || (bytes = ftell(file)) <= 0 ||
        bytes > INT_MAX || bytes % (2 * C2FX_HOP * 2) != 0 ||
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

#define HZ_ONE INT32_C(1048576)

typedef struct pitch_stats {
    int identical;
    int worst;
    int octaves;
    double max_hz;
} pitch_stats;

static int base_voiced(const int16_t *pcm, int n, int center, double hz)
{
    return c2m_voicing_base(c2m_voicing_snr_db(pcm, n, center, hz), c2m_voicing_tilt(pcm, n, center));
}

static void compare(pitch_stats *stats, double model_hz, int32_t fixed_hz_q20)
{
    double hz = fixed_hz_q20 / (double)HZ_ONE;
    int ref = c2m_quant_w0_index(2.0 * C2M_PI * model_hz / C2M_FS_HZ);
    int fixed = c2fx_quant_w0_index(fixed_hz_q20);
    int diff = abs(ref - fixed);
    uint32_t ref_grid_hz = (uint32_t)(50 * 128 + 350 * ref);
    uint32_t fixed_grid_hz = (uint32_t)(50 * 128 + 350 * fixed);
    uint32_t larger = ref_grid_hz > fixed_grid_hz ? ref_grid_hz : fixed_grid_hz;
    uint32_t smaller = ref_grid_hz < fixed_grid_hz ? ref_grid_hz : fixed_grid_hz;

    stats->identical += diff == 0;
    if (diff > stats->worst) {
        stats->worst = diff;
    }
    stats->octaves += (uint64_t)larger * larger > 2 * (uint64_t)smaller * smaller;
    stats->max_hz = fmax(stats->max_hz, fabs(hz - model_hz));
}

static void print_stats(const char *path, const char *tracking, const pitch_stats *stats, int rows)
{
    printf("%s %-5s %4d rows %8.4f%% identical, max index diff %3d, max Hz error %10.5f, octaves %d\n",
           path, tracking, rows, 100.0 * stats->identical / rows, stats->worst, stats->max_hz, stats->octaves);
}

static int run_file(const char *path)
{
    int n;
    int16_t *pcm = read_pcm(path, &n);
    struct c2fx_enc_scratch scratch;
    pitch_stats model_tracking = {0};
    pitch_stats own_tracking = {0};
    double prev_model = 0;
    int32_t prev_fixed = 0;
    int rows = 0;

    if (pcm == NULL) {
        fprintf(stderr, "cannot read whole PCM frames from %s\n", path);
        return 1;
    }
    /* c2m_encode starts at center -160 and analyzes through n + 80. */
    for (int center = -2 * C2FX_HOP; center <= n + C2FX_HOP; center += C2FX_HOP) {
        int16_t x[C2FX_PITCH_SPAN];
        double model = c2m_pitch_hz(pcm, n, center, prev_model);
        int32_t fixed_with_model;
        int32_t fixed_with_own;
        int32_t prev_model_q20 = (int32_t)lround(prev_model * HZ_ONE);

        for (int j = 0; j < C2FX_PITCH_SPAN; j++) {
            int i = center - C2FX_PITCH_BEFORE + j;

            x[j] = i >= 0 && i < n ? pcm[i] : 0;
        }
        fixed_with_model = c2fx_pitch_hz(x, prev_model_q20, &scratch);
        fixed_with_own = prev_fixed == prev_model_q20 ? fixed_with_model : c2fx_pitch_hz(x, prev_fixed, &scratch);
        compare(&model_tracking, model, fixed_with_model);
        compare(&own_tracking, model, fixed_with_own);
        prev_model = base_voiced(pcm, n, center, model) ? model : 0;
        /* Until fixed voicing is implemented, use model base voicing
         * evaluated at the fixed estimator's own pitch.
         */
        prev_fixed = base_voiced(pcm, n, center, fixed_with_own / (double)HZ_ONE) ? fixed_with_own : 0;
        rows++;
    }
    free(pcm);
    print_stats(path, "model", &model_tracking, rows);
    print_stats(path, "own", &own_tracking, rows);
    if ((long)model_tracking.identical * 100 < (long)rows * 98 || model_tracking.worst > 1) {
        fprintf(stderr, "%s: pitch E4 failed (%d/%d identical, worst index difference %d)\n",
                path, model_tracking.identical, rows, model_tracking.worst);
        return 1;
    }
    return 0;
}

static int run_group(const char *pattern, size_t expected)
{
    glob_t paths = {0};
    int failed = 0;

    if (glob(pattern, 0, NULL, &paths) != 0 || paths.gl_pathc != expected) {
        fprintf(stderr, "expected %zu PCM inputs matching %s\n", expected, pattern);
        globfree(&paths);
        return 1;
    }
    for (size_t i = 0; i < paths.gl_pathc; i++) {
        failed |= run_file(paths.gl_pathv[i]);
    }
    globfree(&paths);
    return failed;
}

static int check_edge(const char *name, const int16_t *x)
{
    struct c2fx_enc_scratch scratch;
    int32_t pitch = c2fx_pitch_hz(x, 0, &scratch);

    if (pitch < 50 * HZ_ONE || pitch > 400 * HZ_ONE) {
        fprintf(stderr, "%s: pitch %.6f Hz outside [50, 400]\n", name, pitch / (double)HZ_ONE);
        return 1;
    }
    return 0;
}

static int edge_rows(void)
{
    int16_t x[C2FX_PITCH_SPAN] = {0};
    const int rates[] = {50, 400, 3999};
    uint32_t rng = UINT32_C(0xC2F32001);
    int failed = check_edge("silence", x);

    for (size_t i = 0; i < sizeof rates / sizeof rates[0]; i++) {
        for (int j = 0; j < C2FX_PITCH_SPAN; j++) {
            x[j] = (j * 2 * rates[i] / 8000) % 2 == 0 ? INT16_MIN : INT16_MAX;
        }
        failed |= check_edge("full-scale square wave", x);
    }
    for (int j = 0; j < C2FX_PITCH_SPAN; j++) {
        int32_t value;

        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        value = (int32_t)(rng >> 16);
        x[j] = (int16_t)(value > INT16_MAX ? value - 65536 : value);
    }
    failed |= check_edge("full-scale noise", x);
    for (int j = 0; j < C2FX_PITCH_SPAN; j++) {
        x[j] = j % 160 == 0 ? INT16_MAX : 0;
    }
    return failed | check_edge("50 Hz impulse train", x);
}

static int quantizer_boundaries(void)
{
    const int32_t width = 350 * (HZ_ONE / 128);
    int failed = 0;

    for (int k = 0; k < 127; k++) {
        int32_t mid = 50 * HZ_ONE + k * width + width / 2;

        if (c2fx_quant_w0_index(mid - 1) != k || c2fx_quant_w0_index(mid) != k + 1) {
            fprintf(stderr, "w0 midpoint %d: expected the upper index at the tie\n", k);
            failed = 1;
        }
    }
    return failed;
}

int main(void)
{
    int failed = quantizer_boundaries();

    failed |= edge_rows();
    failed |= run_group("tests/data/voice/*.pcm", 4);
    failed |= run_group("tests/data/signals/*.pcm", 10);
    return failed;
}

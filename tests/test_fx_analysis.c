// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The c2fx Authors

#include "c2fx_internal.h"
#include "c2m_analysis.h"

#include <glob.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define HZ_ONE 65536
#define LOG_ONE 16777216.0

typedef struct analysis_stats {
    int rows;
    int identical;
    int index_diff;
    double energy_db;
    double lsp_hz;
} analysis_stats;

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

static void compare_row(const int16_t *pcm, int n, int center, struct c2fx_enc_scratch *s,
                        analysis_stats *stats)
{
    int16_t x[C2FX_ANALYSIS_LEN];
    c2fx_enc_row row;
    double energy;
    double lsp[C2FX_LSP_ORDER];
    int fixed_d[C2FX_LSP_ORDER];
    int model_d[C2FX_LSP_ORDER];
    int diff;
    double db;

    for (int j = 0; j < C2FX_ANALYSIS_LEN; j++) {
        int i = center - C2FX_ANALYSIS_CENTER + j;

        x[j] = i >= 0 && i < n ? pcm[i] : 0;
    }
    c2fx_lpc_analyze(x, &row, s);
    c2m_lpc_analyze(pcm, n, center, &energy, lsp);
    diff = abs(c2fx_quant_energy_index(&row) - c2m_quant_energy_index(energy));
    c2fx_quant_lsp_indices(&row, fixed_d);
    c2m_quant_lsp_indices(lsp, model_d);

    if ((row.energy_log2_q24 == INT32_MIN) != (energy == 0.0)) {
        db = INFINITY;
    } else if (energy == 0.0) {
        db = 0.0;
    } else {
        db = fabs(row.energy_log2_q24 / LOG_ONE - log2(energy)) * (10.0 * log10(2.0));
    }
    stats->energy_db = fmax(stats->energy_db, db);
    for (int j = 0; j < C2FX_LSP_ORDER; j++) {
        double hz = lsp[j] * C2M_FS_HZ / (2.0 * C2M_PI);
        int delta = abs(fixed_d[j] - model_d[j]);

        if (delta > diff) {
            diff = delta;
        }
        stats->lsp_hz = fmax(stats->lsp_hz, fabs(row.lsp_hz_q16[j] / (double)HZ_ONE - hz));
    }
    if (diff > stats->index_diff) {
        stats->index_diff = diff;
    }
    stats->identical += diff == 0;
    stats->rows++;
}

static void print_stats(const char *name, const analysis_stats *stats)
{
    printf("%-38s %5d rows  %8.4f%% identical  max index diff %2d  %11.7f dB  %11.7f Hz\n",
           name, stats->rows, 100.0 * stats->identical / stats->rows, stats->index_diff,
           stats->energy_db, stats->lsp_hz);
}

static int run_file(const char *path, analysis_stats *pooled)
{
    int n;
    int16_t *pcm = read_pcm(path, &n);
    struct c2fx_enc_scratch scratch;
    analysis_stats stats = {0};

    if (pcm == NULL) {
        fprintf(stderr, "cannot read whole PCM frames from %s\n", path);
        return 1;
    }
    /* Include c2m_encode's leading context row and its three trailing
     * rows along with the two numbered rows per encoded frame.
     */
    for (int s = -1; s <= n / C2FX_HOP + 2; s++) {
        compare_row(pcm, n, (s - 1) * C2FX_HOP, &scratch, &stats);
    }
    free(pcm);
    print_stats(path, &stats);
    pooled->rows += stats.rows;
    pooled->identical += stats.identical;
    if (stats.index_diff > pooled->index_diff) {
        pooled->index_diff = stats.index_diff;
    }
    pooled->energy_db = fmax(pooled->energy_db, stats.energy_db);
    pooled->lsp_hz = fmax(pooled->lsp_hz, stats.lsp_hz);
    if ((long)stats.identical * 100 < (long)stats.rows * 98 || stats.index_diff > 1) {
        fprintf(stderr, "%s: E4 failed (%d/%d identical, worst index difference %d)\n",
                path, stats.identical, stats.rows, stats.index_diff);
        return 1;
    }
    return 0;
}

static int run_group(const char *pattern, analysis_stats *pooled)
{
    glob_t paths = {0};
    int failed = 0;

    if (glob(pattern, 0, NULL, &paths) != 0) {
        fprintf(stderr, "cannot list PCM inputs: %s\n", pattern);
        globfree(&paths);
        return 1;
    }
    for (size_t i = 0; i < paths.gl_pathc; i++) {
        failed |= run_file(paths.gl_pathv[i], pooled);
    }
    globfree(&paths);
    return failed;
}

static int check_edge(const char *name, const int16_t *x, struct c2fx_enc_scratch *s, int silence)
{
    c2fx_enc_row row;
    int d[C2FX_LSP_ORDER];
    int e;

    c2fx_lpc_analyze(x, &row, s);
    e = c2fx_quant_energy_index(&row);
    c2fx_quant_lsp_indices(&row, d);
    if (e < 0 || e > 31 || (silence && (e != 0 || row.energy_log2_q24 != INT32_MIN))) {
        fprintf(stderr, "%s: unexpected energy index %d or zero-energy flag\n", name, e);
        return 1;
    }
    for (int j = 0; j < C2FX_LSP_ORDER; j++) {
        int32_t hz = row.lsp_hz_q16[j];

        if (d[j] < 0 || d[j] > 31 || hz < 0 || hz > INT32_C(4000) * HZ_ONE ||
            (j > 0 && hz <= row.lsp_hz_q16[j - 1]) ||
            (silence && hz != j * 400 * HZ_ONE)) {
            fprintf(stderr, "%s: unexpected LSP %d (index %d, %.6f Hz)\n",
                    name, j, d[j], hz / (double)HZ_ONE);
            return 1;
        }
    }
    return 0;
}

static int edge_rows(void)
{
    int16_t x[C2FX_ANALYSIS_LEN];
    struct c2fx_enc_scratch scratch;
    uint32_t rng = UINT32_C(0xC2F32001);
    int failed;

    for (int j = 0; j < C2FX_ANALYSIS_LEN; j++) {
        x[j] = (j / 20) % 2 == 0 ? 32767 : -32767;
    }
    failed = check_edge("full-scale square wave", x, &scratch, 0);

    for (int j = 0; j < C2FX_ANALYSIS_LEN; j++) {
        int32_t word;

        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        word = (int32_t)(rng >> 16);
        x[j] = (int16_t)(word > INT16_MAX ? word - 65536 : word);
        if (x[j] == INT16_MIN) {
            x[j] = -32767;
        }
    }
    failed |= check_edge("full-scale noise", x, &scratch, 0);

    for (int j = 0; j < C2FX_ANALYSIS_LEN; j++) {
        x[j] = 0;
    }
    failed |= check_edge("digital silence", x, &scratch, 1);
    return failed;
}

static int hann_symmetry(void)
{
    int16_t x[C2FX_ANALYSIS_LEN];
    struct c2fx_enc_scratch scratch;
    c2fx_enc_row row;

    for (int j = 0; j < C2FX_ANALYSIS_LEN; j++) {
        x[j] = -32767;
    }
    c2fx_lpc_analyze(x, &row, &scratch);
    for (int j = 0; j < C2FX_ANALYSIS_CENTER; j++) {
        int mirror = C2FX_ANALYSIS_LEN - 1 - j;

        if (scratch.windowed[j] != scratch.windowed[mirror]) {
            fprintf(stderr, "Hann symmetry: x[%d] and x[%d] window to %d and %d\n",
                    j, mirror, scratch.windowed[j], scratch.windowed[mirror]);
            return 1;
        }
    }
    return 0;
}

static int reflection_cap(void)
{
    const int64_t one = INT64_C(1) << 52;

    for (int sign = -1; sign <= 1; sign += 2) {
        /* Apply the model's order-one equations to a direct lag pair.
         * The unconstrained |r[1]/r[0]| = 1 - 2^-21 exceeds the cap.
         */
        int64_t r[] = {one, sign * (one - (one >> 21))};
        int64_t a[2] = {0};
        uint64_t energy = c2fx_levinson_q52(r, a, 1);
        double rho = (double)a[1] / (double)one;
        double model_energy = 1.0 - C2M_LPC_RHO_MAX * C2M_LPC_RHO_MAX;

        if (fabs(rho + sign * C2M_LPC_RHO_MAX) > 1.0 / (double)one ||
            fabs((double)energy / (double)one - model_energy) > 2e-15) {
            fprintf(stderr, "reflection cap: lag sign %d gives rho %.9f and energy %.9g\n",
                    sign, rho, (double)energy / (double)one);
            return 1;
        }
    }
    return 0;
}

static int quantizer_boundaries(void)
{
    c2fx_enc_row row = {0};
    int d[C2FX_LSP_ORDER];

    for (int k = 0; k < 31; k++) {
        double log2_midpoint = ((-10.0 + (k + 0.5) * 50.0 / 32.0) / 10.0) * log2(10.0);
        int32_t first_above = (int32_t)ceil(log2_midpoint * LOG_ONE);
        int above;
        int below;

        row.energy_log2_q24 = first_above;
        above = c2fx_quant_energy_index(&row);
        row.energy_log2_q24--;
        below = c2fx_quant_energy_index(&row);
        if (below != k || above != k + 1) {
            fprintf(stderr, "energy midpoint %d: indices %d,%d, expected %d,%d\n",
                    k, below, above, k, k + 1);
            return 1;
        }
    }

    /* First target is halfway between 25 and 50 Hz. After taking 25 Hz,
     * the second target is just above the same midpoint.
     */
    row.lsp_hz_q16[0] = 75 * (HZ_ONE / 2);
    row.lsp_hz_q16[1] = 125 * (HZ_ONE / 2) + 1;
    c2fx_quant_lsp_indices(&row, d);
    if (d[0] != 0 || d[1] != 1) {
        fprintf(stderr, "LSP midpoints: indices %d,%d, expected 0,1\n", d[0], d[1]);
        return 1;
    }
    return 0;
}

int main(void)
{
    analysis_stats pooled = {0};
    int failed = quantizer_boundaries();

    failed |= edge_rows();
    failed |= hann_symmetry();
    failed |= reflection_cap();
    failed |= run_group("tests/data/voice/*.pcm", &pooled);
    failed |= run_group("tests/data/signals/*.pcm", &pooled);
    if (pooled.rows != 0) {
        print_stats("pooled", &pooled);
    }
    printf("encoder scratch %zu bytes, scratch union %zu bytes\n",
           sizeof(struct c2fx_enc_scratch), sizeof(c2fx_scratch_t));
    return failed;
}

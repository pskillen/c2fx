// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The c2fx Authors

#define _POSIX_C_SOURCE 200809L

#include "c2m_lpc.h"

#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_SETS 64
#define NAME_LEN 256

static const uint8_t example_frame[C2M_FRAME_BYTES] = {
    0xd6, 0x75, 0xef, 0x7b, 0xde, 0xf7, 0xbd, 0xef
};

static int ends_with(const char *name, const char *suffix)
{
    size_t name_len = strlen(name);
    size_t suffix_len = strlen(suffix);

    if (name_len < suffix_len) {
        return 0;
    }
    return strcmp(name + name_len - suffix_len, suffix) == 0;
}

static int name_less(const void *left, const void *right)
{
    return strcmp(left, right);
}

static int example_lpc(double a[C2M_LPC_ORDER + 1], double *energy_db)
{
    c2m_indices idx;
    c2m_params params;

    if (c2m_frame_unpack(example_frame, &idx) != 0) {
        return 1;
    }
    c2m_dequant(&idx, &params);
    c2m_lsp_to_lpc(params.lsp_rad, a);
    *energy_db = params.energy_db;
    return 0;
}

static int test_rescale(void)
{
    double a[C2M_LPC_ORDER + 1];
    double raw[C2M_SPECTRUM_BINS];
    double filtered[C2M_SPECTRUM_BINS];
    double sum_raw = 0.0;
    double sum_out = 0.0;
    double rel;

    /* A mild one-pole filter. Its post-filter moves the bin sum, and the
     * 1e-4 guard then leaves that sum within 1e-9 of the original.
     */
    for (int k = 0; k <= C2M_LPC_ORDER; k++) {
        a[k] = 0.0;
    }
    a[0] = 1.0;
    a[1] = -0.1;
    c2m_lpc_power(a, raw);
    c2m_postfilter_power(a, raw, filtered);
    for (int k = 0; k < C2M_SPECTRUM_BINS; k++) {
        sum_raw += raw[k];
    }
    c2m_rescale_power(raw, filtered, C2M_SPECTRUM_BINS);
    for (int k = 0; k < C2M_SPECTRUM_BINS; k++) {
        sum_out += filtered[k];
    }
    rel = fabs(sum_out - sum_raw) / sum_raw;
    if (!(rel < 1e-9)) {
        fprintf(stderr, "post-filter rescale relative sum error %.3e\n", rel);
        return 1;
    }
    return 0;
}

static int test_bass_boost(void)
{
    double a[C2M_LPC_ORDER + 1];
    double raw[C2M_SPECTRUM_BINS];
    double pre[C2M_SPECTRUM_BINS];
    double shaped[C2M_SPECTRUM_BINS];
    double energy_db;
    double energy;

    if (example_lpc(a, &energy_db) != 0) {
        return 1;
    }
    c2m_lpc_power(a, raw);
    c2m_postfilter_power(a, raw, pre);
    c2m_rescale_power(raw, pre, C2M_SPECTRUM_BINS);
    energy = c2m_energy_power(energy_db);
    for (int k = 0; k < C2M_SPECTRUM_BINS; k++) {
        pre[k] *= energy;
    }
    c2m_spectrum_shape(a, energy_db, shaped);
    for (int k = 0; k < C2M_SPECTRUM_BINS; k++) {
        double expect = pre[k];
        double rel;

        /* Bins 0..63 only. 64 is the first bin that stays unboosted. */
        if (k < 64) {
            expect *= 1.96;
        }
        rel = fabs(shaped[k] - expect) / expect;
        if (!(rel < 1e-9)) {
            fprintf(stderr, "bass boost bin %d relative error %.3e\n", k, rel);
            return 1;
        }
    }
    return 0;
}

static double grid_power(int e)
{
    return pow(10.0, (-10.0 + (50.0 / 32.0) * (double)e) / 10.0);
}

static int nearest_energy_index(double csv, double *rel_out)
{
    int best = 0;
    double best_rel = 1e300;

    for (int e = 0; e <= 31; e++) {
        double pred = (double)(float)grid_power(e);
        double rel = fabs(csv - pred) / pred;

        if (rel < best_rel) {
            best_rel = rel;
            best = e;
        }
    }
    *rel_out = best_rel;
    return best;
}

static int read_energy(FILE *csv, int *subframe, double *energy)
{
    char line[512];
    double w0;
    double voicing;
    double lsp[C2M_LSP_ORDER];
    int n;

    if (fgets(line, (int)sizeof line, csv) == NULL) {
        return 1;
    }
    n = sscanf(line, "%d,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf",
               subframe, &w0, &voicing, energy,
               &lsp[0], &lsp[1], &lsp[2], &lsp[3], &lsp[4],
               &lsp[5], &lsp[6], &lsp[7], &lsp[8], &lsp[9]);
    if (n < 14) {
        fprintf(stderr, "bad parameter row: %s", line);
        return -1;
    }
    return 0;
}

static int check_set(const char *dir, const char *c2_name, double *worst)
{
    char path[512];
    char base[NAME_LEN];
    FILE *bits;
    FILE *csv;
    size_t base_len;
    int frame = 0;

    base_len = strlen(c2_name) - 3;
    if (base_len >= NAME_LEN) {
        return 1;
    }
    memcpy(base, c2_name, base_len);
    base[base_len] = '\0';
    snprintf(path, sizeof path, "%s/%s", dir, c2_name);
    bits = fopen(path, "rb");
    snprintf(path, sizeof path, "%s/%s_params.csv", dir, base);
    csv = fopen(path, "r");
    if (bits == NULL || csv == NULL) {
        fprintf(stderr, "missing %s bitstream or parameter csv\n", base);
        if (bits != NULL) {
            fclose(bits);
        }
        if (csv != NULL) {
            fclose(csv);
        }
        return 1;
    }
    if (fgets(path, (int)sizeof path, csv) == NULL || strncmp(path, "subframe,", 9) != 0) {
        fprintf(stderr, "%s parameter csv has no header\n", base);
        fclose(bits);
        fclose(csv);
        return 1;
    }
    for (;;) {
        uint8_t raw[C2M_FRAME_BYTES];
        c2m_indices idx;
        size_t got = fread(raw, 1, C2M_FRAME_BYTES, bits);
        int sub_a;
        int sub_b;
        double energy_a;
        double energy_b;
        double rel_a;
        double rel_b;
        int e_b;
        double decoded;

        if (got == 0) {
            break;
        }
        if (got != C2M_FRAME_BYTES) {
            fprintf(stderr, "%s frame %d is truncated\n", base, frame);
            fclose(bits);
            fclose(csv);
            return 1;
        }
        if (read_energy(csv, &sub_a, &energy_a) != 0 ||
            read_energy(csv, &sub_b, &energy_b) != 0) {
            fprintf(stderr, "%s frame %d has no parameter pair\n", base, frame);
            fclose(bits);
            fclose(csv);
            return 1;
        }
        c2m_frame_unpack(raw, &idx);
        nearest_energy_index(energy_a, &rel_a);
        e_b = nearest_energy_index(energy_b, &rel_b);
        if (rel_a > *worst) {
            *worst = rel_a;
        }
        if (rel_b > *worst) {
            *worst = rel_b;
        }
        /* Float32 rounding of the grid is about 5e-8; the CSV text adds a little. */
        if (!(rel_a < 1e-6) || !(rel_b < 1e-6)) {
            fprintf(stderr, "%s frame %d energy rel %.3e %.3e\n", base, frame, rel_a, rel_b);
            fclose(bits);
            fclose(csv);
            return 1;
        }
        if (e_b != idx.e) {
            fprintf(stderr, "%s frame %d sub-frame b energy index %d, frame e %d\n",
                    base, frame, e_b, idx.e);
            fclose(bits);
            fclose(csv);
            return 1;
        }
        decoded = c2m_energy_power(C2M_ENERGY_STEP_DB * (double)idx.e);
        if (fabs(decoded - grid_power(idx.e)) / grid_power(idx.e) > 1e-12) {
            fprintf(stderr, "%s frame %d dequantizer left the energy grid\n", base, frame);
            fclose(bits);
            fclose(csv);
            return 1;
        }
        frame += 1;
    }
    fclose(bits);
    fclose(csv);
    return 0;
}

static int test_energy_grid(void)
{
    const char *dir = "vectors";
    DIR *dp;
    struct dirent *ent;
    char names[MAX_SETS][NAME_LEN];
    int count = 0;
    double worst = 0.0;

    for (int e = 0; e <= 31; e++) {
        double got = c2m_energy_power(C2M_ENERGY_STEP_DB * (double)e);
        double expect = grid_power(e);

        if (fabs(got - expect) / expect > 1e-12) {
            fprintf(stderr, "energy index %d power %.9g, grid %.9g\n", e, got, expect);
            return 1;
        }
    }
    dp = opendir(dir);
    if (dp == NULL) {
        fprintf(stderr, "cannot open %s (run from the repository root)\n", dir);
        return 1;
    }
    while ((ent = readdir(dp)) != NULL) {
        if (!ends_with(ent->d_name, ".c2")) {
            continue;
        }
        if (count >= MAX_SETS || strlen(ent->d_name) >= NAME_LEN) {
            fprintf(stderr, "too many bitstreams in %s\n", dir);
            closedir(dp);
            return 1;
        }
        memcpy(names[count], ent->d_name, strlen(ent->d_name) + 1);
        count += 1;
    }
    closedir(dp);
    qsort(names, (size_t)count, NAME_LEN, name_less);
    for (int i = 0; i < count; i++) {
        if (check_set(dir, names[i], &worst) != 0) {
            return 1;
        }
    }
    printf("energy grid: worst relative error vs float32 %.3e\n", worst);
    return 0;
}

int main(void)
{
    if (test_rescale() != 0 || test_bass_boost() != 0 || test_energy_grid() != 0) {
        return 1;
    }
    return 0;
}

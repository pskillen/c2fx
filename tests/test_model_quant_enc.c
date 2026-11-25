// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The c2fx Authors

#define _POSIX_C_SOURCE 200809L

#include "c2m_quant.h"

#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Spec 10.1. The CSV holds unquantized LSPs, quantized w0 and energy, and the
 * voicing flags. Re-quantizing those numbers is the index in vectors/<set>.c2.
 */

#define MAX_SETS 64
#define NAME_LEN 256

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

static int read_row(
    FILE *csv,
    int *subframe,
    double *w0,
    int *voiced,
    double *energy,
    double lsp[C2M_LSP_ORDER]
)
{
    char line[512];
    double voicing;
    int n;

    if (fgets(line, (int)sizeof line, csv) == NULL) {
        return 1;
    }
    n = sscanf(
        line,
        "%d,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf",
        subframe,
        w0,
        &voicing,
        energy,
        &lsp[0],
        &lsp[1],
        &lsp[2],
        &lsp[3],
        &lsp[4],
        &lsp[5],
        &lsp[6],
        &lsp[7],
        &lsp[8],
        &lsp[9]
    );
    if (n != 14) {
        fprintf(stderr, "bad parameter row: %s", line);
        return -1;
    }
    *voiced = voicing >= 0.5;
    return 0;
}

static int same_indices(const c2m_indices *a, const c2m_indices *b)
{
    if (a->v_a != b->v_a || a->v_b != b->v_b || a->i != b->i || a->e != b->e) {
        return 0;
    }
    for (int j = 0; j < C2M_LSP_ORDER; j++) {
        if (a->d[j] != b->d[j]) {
            return 0;
        }
    }
    return 1;
}

static int check_set(const char *c2_name)
{
    char base[NAME_LEN];
    char path[512];
    FILE *bits;
    FILE *csv;
    size_t base_len = strlen(c2_name) - 3;
    int frame = 0;
    int failed = 0;

    if (base_len >= NAME_LEN) {
        fprintf(stderr, "name too long: %s\n", c2_name);
        return 1;
    }
    memcpy(base, c2_name, base_len);
    base[base_len] = '\0';
    snprintf(path, sizeof path, "vectors/%s", c2_name);
    bits = fopen(path, "rb");
    snprintf(path, sizeof path, "vectors/%s_params.csv", base);
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
        c2m_indices got;
        c2m_indices expect;
        size_t nread = fread(raw, 1, C2M_FRAME_BYTES, bits);
        int sub_a;
        int sub_b;
        int voiced_a;
        int voiced_b;
        double w0_a;
        double w0_b;
        double energy_a;
        double energy_b;
        double lsp_a[C2M_LSP_ORDER];
        double lsp_b[C2M_LSP_ORDER];

        if (nread == 0) {
            break;
        }
        if (nread != C2M_FRAME_BYTES) {
            fprintf(stderr, "%s frame %d is truncated\n", base, frame);
            failed = 1;
            break;
        }
        if (read_row(csv, &sub_a, &w0_a, &voiced_a, &energy_a, lsp_a) != 0 ||
            read_row(csv, &sub_b, &w0_b, &voiced_b, &energy_b, lsp_b) != 0) {
            fprintf(stderr, "%s frame %d has no parameter pair\n", base, frame);
            failed = 1;
            break;
        }
        c2m_frame_unpack(raw, &expect);
        c2m_quant_encode(voiced_a, voiced_b, w0_b, energy_b, lsp_b, &got);
        if (!same_indices(&got, &expect)) {
            fprintf(
                stderr,
                "%s frame %d quantizer mismatch v %d,%d i %d e %d (bitstream %d,%d i %d e %d)\n",
                base,
                frame,
                got.v_a,
                got.v_b,
                got.i,
                got.e,
                expect.v_a,
                expect.v_b,
                expect.i,
                expect.e
            );
            failed = 1;
            break;
        }
        frame += 1;
    }
    fclose(bits);
    fclose(csv);
    if (!failed) {
        printf("%s: %d frames match\n", base, frame);
    }
    return failed;
}

static int test_worked_points(void)
{
    double w0 = 2.0 * C2M_PI * 139.40 / C2M_FS_HZ;
    int w0_index = c2m_quant_w0_index(w0);
    int energy_index = c2m_quant_energy_index(34.52);

    if (w0_index != 33) {
        fprintf(stderr, "w0 of 139.40 Hz quantizes to %d, expected 33\n", w0_index);
        return 1;
    }
    if (energy_index != 16) {
        fprintf(stderr, "energy 34.52 quantizes to %d, expected 16\n", energy_index);
        return 1;
    }
    if (c2m_quant_energy_index(0.0) != 0 || c2m_quant_energy_index(-1.0) != 0) {
        fprintf(stderr, "non-positive energy did not quantize to 0\n");
        return 1;
    }
    return 0;
}

int main(void)
{
    DIR *dp;
    struct dirent *ent;
    char names[MAX_SETS][NAME_LEN];
    int count = 0;
    int failed = 0;

    if (test_worked_points() != 0) {
        return 1;
    }
    dp = opendir("vectors");
    if (dp == NULL) {
        fprintf(stderr, "cannot open vectors (run from the repository root)\n");
        return 1;
    }
    while ((ent = readdir(dp)) != NULL) {
        if (!ends_with(ent->d_name, ".c2")) {
            continue;
        }
        if (count >= MAX_SETS || strlen(ent->d_name) >= NAME_LEN) {
            fprintf(stderr, "too many bitstreams\n");
            closedir(dp);
            return 1;
        }
        memcpy(names[count], ent->d_name, strlen(ent->d_name) + 1);
        count += 1;
    }
    closedir(dp);
    qsort(names, (size_t)count, NAME_LEN, name_less);
    for (int i = 0; i < count; i++) {
        if (check_set(names[i]) != 0) {
            failed = 1;
        }
    }
    if (!failed && count != 14) {
        fprintf(stderr, "expected 14 vector sets, found %d\n", count);
        failed = 1;
    }
    return failed ? 1 : 0;
}

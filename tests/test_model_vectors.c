// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The c2fx Authors

#define _POSIX_C_SOURCE 200809L

#include "c2m_decode.h"

#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* CSV w0 is float32 of the quantized pitch; 1e-6 still catches an off-by-one
 * index (grid step ~2.15e-3 rad) and float32 rounding (observed max ~3.3e-8).
 * Sub-frame a rows in the CSV are on the quantizer grid, not interpolated
 * midpoints, so this test compares sub-frame b only.
 * Frame k lines up with CSV rows 2k (sub-frame a) and 2k+1 (sub-frame b).
 */
#define W0_ABS_TOL 1e-6
#define LSP_ABS_TOL_TONE 0.05
#define LSP_ABS_TOL_SPEECH 0.5

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
    double lsp_rad[C2M_LSP_ORDER]
)
{
    char line[512];
    double voicing;
    double energy;
    double lsp[C2M_LSP_ORDER];
    int n;

    if (fgets(line, (int)sizeof line, csv) == NULL) {
        return 1;
    }
    n = sscanf(line, "%d,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf",
               subframe, w0, &voicing, &energy,
               &lsp[0], &lsp[1], &lsp[2], &lsp[3], &lsp[4],
               &lsp[5], &lsp[6], &lsp[7], &lsp[8], &lsp[9]);
    if (n < 3) {
        fprintf(stderr, "bad parameter row: %s", line);
        return -1;
    }
    *voiced = voicing >= 0.5;
    if (lsp_rad != NULL) {
        if (n < 14) {
            fprintf(stderr, "LSP columns missing: %s", line);
            return -1;
        }
        for (int j = 0; j < C2M_LSP_ORDER; j++) {
            lsp_rad[j] = lsp[j];
        }
    }
    return 0;
}

static int is_tone_set(const char *base)
{
    if (strncmp(base, "sine_", 5) == 0) {
        return 1;
    }
    if (strcmp(base, "dc_8000") == 0 || strcmp(base, "noise_xorshift") == 0 ||
        strcmp(base, "silence") == 0) {
        return 1;
    }
    return 0;
}

static int check_params(const char *name, int frame, const c2m_params *params)
{
    if (!(params->w0 > 0.0 && params->w0 < 1.0)) {
        fprintf(stderr, "%s frame %d w0 %.6g out of range\n", name, frame, params->w0);
        return 1;
    }
    if (!(params->f0_hz >= 50.0 && params->f0_hz < 400.0)) {
        fprintf(stderr, "%s frame %d f0 %.6g out of range\n", name, frame, params->f0_hz);
        return 1;
    }
    if (!(params->energy_db >= 0.0)) {
        fprintf(stderr, "%s frame %d energy %.6g out of range\n", name, frame, params->energy_db);
        return 1;
    }
    if (!(params->lsp_hz[C2M_LSP_ORDER - 1] > 0.0 &&
          params->lsp_hz[C2M_LSP_ORDER - 1] == params->lsp_hz[C2M_LSP_ORDER - 1])) {
        fprintf(stderr, "%s frame %d LSP sum %.6g out of range\n",
                name, frame, params->lsp_hz[C2M_LSP_ORDER - 1]);
        return 1;
    }
    return 0;
}

static int check_set(const char *dir, const char *c2_name)
{
    char path[512];
    char base[NAME_LEN];
    FILE *bits;
    FILE *csv;
    c2m_decode state;
    size_t base_len;
    int frame = 0;
    int voiced_n = 0;
    int voiced_match = 0;
    int unvoiced_n = 0;
    int unvoiced_match = 0;
    double voiced_max = 0.0;
    double unvoiced_max = 0.0;
    double lsp_tol;
    int lsp_n = 0;
    int lsp_match = 0;
    double lsp_max = 0.0;
    double lsp_sum = 0.0;
    int failed = 0;

    base_len = strlen(c2_name) - 3;
    if (base_len >= NAME_LEN) {
        fprintf(stderr, "name too long: %s\n", c2_name);
        return 1;
    }
    memcpy(base, c2_name, base_len);
    base[base_len] = '\0';
    lsp_tol = is_tone_set(base) ? LSP_ABS_TOL_TONE : LSP_ABS_TOL_SPEECH;

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

    c2m_decode_init(&state);
    for (;;) {
        uint8_t raw[C2M_FRAME_BYTES];
        c2m_indices idx;
        c2m_params params;
        c2m_subframe sub[2];
        size_t got = fread(raw, 1, C2M_FRAME_BYTES, bits);
        int sub_a;
        int sub_b;
        int voiced_a;
        int voiced_b;
        double w0_a;
        double w0_b;
        double lsp_b[C2M_LSP_ORDER];

        if (got == 0) {
            break;
        }
        if (got != C2M_FRAME_BYTES) {
            fprintf(stderr, "%s frame %d is truncated\n", base, frame);
            failed = 1;
            break;
        }
        if (read_row(csv, &sub_a, &w0_a, &voiced_a, NULL) != 0 ||
            read_row(csv, &sub_b, &w0_b, &voiced_b, lsp_b) != 0) {
            fprintf(stderr, "%s frame %d has no parameter pair\n", base, frame);
            failed = 1;
            break;
        }
        if (sub_a != 2 * frame || sub_b != 2 * frame + 1) {
            fprintf(stderr, "%s frame %d lines up with csv rows %d and %d\n",
                    base, frame, sub_a, sub_b);
            failed = 1;
            break;
        }
        if (c2m_frame_unpack(raw, &idx) != 0) {
            fprintf(stderr, "%s frame %d did not unpack\n", base, frame);
            failed = 1;
            break;
        }
        c2m_dequant(&idx, &params);
        if (check_params(base, frame, &params) != 0) {
            failed = 1;
            break;
        }
        c2m_decode_frame(&state, &idx, sub);
        if (sub[0].voiced != voiced_a || sub[1].voiced != voiced_b) {
            fprintf(stderr, "%s frame %d voicing model %d,%d csv %d,%d\n",
                    base, frame, sub[0].voiced, sub[1].voiced, voiced_a, voiced_b);
            failed = 1;
            break;
        }

        if (voiced_b) {
            double err = fabs(sub[1].w0 - w0_b);

            voiced_n += 1;
            if (err > voiced_max) {
                voiced_max = err;
            }
            if (err <= W0_ABS_TOL) {
                voiced_match += 1;
            } else {
                fprintf(stderr, "%s frame %d sub-frame b w0 %.9g csv %.9g\n",
                        base, frame, sub[1].w0, w0_b);
                failed = 1;
            }
        } else {
            double err = fabs(sub[1].w0 - w0_b);

            unvoiced_n += 1;
            if (err > unvoiced_max) {
                unvoiced_max = err;
            }
            if (err <= W0_ABS_TOL) {
                unvoiced_match += 1;
            }
        }

        if (voiced_b) {
            double frame_max = 0.0;

            lsp_n += 1;
            for (int j = 0; j < C2M_LSP_ORDER; j++) {
                double err = fabs(sub[1].lsp_rad[j] - lsp_b[j]);

                if (err > frame_max) {
                    frame_max = err;
                }
            }
            lsp_sum += frame_max;
            if (frame_max > lsp_max) {
                lsp_max = frame_max;
            }
            if (frame_max <= lsp_tol) {
                lsp_match += 1;
            } else if (is_tone_set(base)) {
                fprintf(stderr, "%s frame %d sub-frame b LSP max %.4g csv mismatch\n",
                        base, frame, frame_max);
                failed = 1;
            }
        }
        frame += 1;
    }

    if (!failed) {
        int extra_sub = 0;
        int extra_voiced = 0;
        double extra_w0 = 0.0;
        int extra = read_row(csv, &extra_sub, &extra_w0, &extra_voiced, NULL);

        if (extra == 0) {
            fprintf(stderr, "%s has parameter rows past the bitstream\n", base);
            failed = 1;
        } else if (extra < 0) {
            fprintf(stderr, "%s has a trailing parameter row that did not parse\n", base);
            failed = 1;
        }
    }

    fclose(bits);
    fclose(csv);

    if (!failed) {
        double rate = voiced_n == 0 ? 1.0 : (double)voiced_match / (double)voiced_n;
        double urate = unvoiced_n == 0 ? 1.0 : (double)unvoiced_match / (double)unvoiced_n;
        double lrate = lsp_n == 0 ? 1.0 : (double)lsp_match / (double)lsp_n;
        double lmean = lsp_n == 0 ? 0.0 : lsp_sum / (double)lsp_n;

        printf("%s: frames %d voiced_b %d/%d rate %.4f max_abs %.3e"
               " unvoiced_b %d/%d rate %.4f max_abs %.3e"
               " lsp_b %d/%d rate %.4f max %.3e mean_max %.3e tol %.2f\n",
               base, frame, voiced_match, voiced_n, rate, voiced_max,
               unvoiced_match, unvoiced_n, urate, unvoiced_max,
               lsp_match, lsp_n, lrate, lsp_max, lmean, lsp_tol);
        if (!is_tone_set(base) && lmean > lsp_tol) {
            fprintf(stderr, "%s speech LSP mean max %.3e above tol %.2f\n", base, lmean, lsp_tol);
            failed = 1;
        }
    }
    return failed;
}

int main(void)
{
    const char *dir = "vectors";
    DIR *dp;
    struct dirent *ent;
    char names[MAX_SETS][NAME_LEN];
    int count = 0;
    int failed = 0;

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
    if (count == 0) {
        fprintf(stderr, "no .c2 files in %s\n", dir);
        return 1;
    }
    qsort(names, (size_t)count, NAME_LEN, name_less);

    printf("alignment: codec frame k <-> CSV rows 2k (sub-frame a) and 2k+1 (sub-frame b)\n");
    printf("compare: decoded sub-frame b w0 and LSPs vs CSV;"
           " w0 tol %g; LSP tol %.2f tone / %.1f speech (per-frame max rad)\n",
           W0_ABS_TOL, LSP_ABS_TOL_TONE, LSP_ABS_TOL_SPEECH);
    for (int i = 0; i < count; i++) {
        if (check_set(dir, names[i]) != 0) {
            failed = 1;
        }
    }
    return failed ? 1 : 0;
}

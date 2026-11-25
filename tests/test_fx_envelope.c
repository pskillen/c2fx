// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The c2fx Authors
// Fixed-point envelope against the float model on every vector sub-frame.
// w0 matches round(2^32 * f0/8000), the phase step. LSP q1 matches
// round(hz * 2), the table's 0.5 Hz unit. Harmonic amplitudes are log2
// Q11 and must stay within 0.25 dB of the model.

#include "c2fx_internal.h"
#include "c2m_decode.h"
#include "c2m_frame.h"
#include "c2m_lpc.h"

#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_SETS 32
#define NAME_LEN 64
#define AMP_DB 0.25

static int ends_with(const char *name, const char *suffix)
{
    size_t n = strlen(name);
    size_t s = strlen(suffix);

    return n >= s && memcmp(name + n - s, suffix, s) == 0;
}

static int name_less(const void *a, const void *b)
{
    return strcmp((const char *)a, (const char *)b);
}

#ifndef C2M_POSTFILTER_PDF
static int cmp_double(const void *a, const void *b)
{
    double x = *(const double *)a;
    double y = *(const double *)b;

    return (x > y) - (x < y);
}
#endif

static int run_file(const char *name)
{
    char path[128];
    FILE *fp;
    c2fx_front fx;
    c2m_decode md;
    int bad_l = 0;
    int bad_w = 0;
    int bad_lsp = 0;
#ifndef C2M_POSTFILTER_PDF
    double *errs = NULL;
    int nerr = 0;
    int cap = 0;
    double worst = 0.0;
    const double db_per_q11 = 20.0 * log10(2.0) / 2048.0;
    int within = 0;
    double p99;
#endif

    snprintf(path, sizeof path, "vectors/%s", name);
    fp = fopen(path, "rb");
    if (fp == NULL) {
        fprintf(stderr, "cannot read %s\n", path);
        return 1;
    }
    c2fx_front_init(&fx);
    c2m_decode_init(&md);
    for (;;) {
        uint8_t bytes[8];
        c2m_indices midx;
        c2fx_indices fidx;
        c2m_subframe ms[2];
        c2fx_subframe fs[2];
        size_t n = fread(bytes, 1, 8, fp);

        if (n == 0) {
            break;
        }
        if (n != 8) {
            fprintf(stderr, "%s: trailing %zu bytes\n", name, n);
            fclose(fp);
#ifndef C2M_POSTFILTER_PDF
            free(errs);
#endif
            return 1;
        }
        c2m_frame_unpack(bytes, &midx);
        c2fx_frame_unpack(bytes, &fidx);
        c2m_decode_frame(&md, &midx, ms);
        c2fx_dequant_frame(&fx, &fidx, fs);
        for (int s = 0; s < 2; s++) {
            double a[C2M_LPC_ORDER + 1];
            c2m_envelope env;
            uint32_t model_w0 = (uint32_t)llround(ms[s].f0_hz / 8000.0 * 4294967296.0);

            c2m_lsp_to_lpc(ms[s].lsp_rad, a);
            c2m_envelope_sample(a, ms[s].w0, ms[s].energy_db, &env);
            c2fx_envelope(&fs[s]);
            if (fs[s].w0 != model_w0) {
                bad_w += 1;
            }
            if (fs[s].count != env.count) {
                bad_l += 1;
            }
            for (int j = 0; j < C2FX_LSP_ORDER; j++) {
                int q1 = (int)llround(ms[s].lsp_hz[j] * 2.0);

                if (q1 != fs[s].lsp_q1[j]) {
                    bad_lsp += 1;
                }
            }
#ifndef C2M_POSTFILTER_PDF
            for (int m = 0; m < env.count; m++) {
                double err;

                if (env.amp[m] <= 0.0) {
                    err = (fs[s].amp_log2_q11[m] == (int16_t)-32768) ? 0.0 : 100.0;
                } else {
                    double model_q11 = log2(env.amp[m]) * 2048.0;

                    err = fabs((double)fs[s].amp_log2_q11[m] - model_q11) * db_per_q11;
                }
                if (err > worst) {
                    worst = err;
                }
                if (nerr >= cap) {
                    cap = cap ? cap * 2 : 4096;
                    errs = realloc(errs, (size_t)cap * sizeof(double));
                    if (errs == NULL) {
                        fprintf(stderr, "out of memory\n");
                        fclose(fp);
                        return 1;
                    }
                }
                errs[nerr++] = err;
            }
#endif
        }
    }
    fclose(fp);
#ifdef C2M_POSTFILTER_PDF
    printf("%-22s L mismatches %d, w0 mismatches %d, LSP mismatches %d\n", name, bad_l, bad_w,
           bad_lsp);
    if (bad_l != 0 || bad_w != 0 || bad_lsp != 0) {
        return 1;
    }
#else
    if (nerr == 0) {
        fprintf(stderr, "%s: no harmonics\n", name);
        free(errs);
        return 1;
    }
    qsort(errs, (size_t)nerr, sizeof(double), cmp_double);
    p99 = errs[(nerr - 1) * 99 / 100];
    for (int i = 0; i < nerr; i++) {
        if (errs[i] <= 0.1) {
            within += 1;
        }
    }
    printf("%-22s worst %7.3f dB  p99 %7.3f  within 0.1 dB %5.1f%%\n", name, worst, p99,
           100.0 * (double)within / (double)nerr);
    free(errs);
    if (bad_l != 0 || bad_w != 0 || bad_lsp != 0 || worst > AMP_DB) {
        fprintf(stderr, "%s: L mismatches %d, w0 mismatches %d, LSP mismatches %d, worst %.3f dB\n",
                name, bad_l, bad_w, bad_lsp, worst);
        return 1;
    }
#endif
    return 0;
}

int main(void)
{
    DIR *dp = opendir("vectors");
    struct dirent *ent;
    char names[MAX_SETS][NAME_LEN];
    int count = 0;
    int failed = 0;

    printf("w0 is round(2^32 * f0/8000). LSP q1 is round(hz * 2), the 0.5 Hz unit.\n");
/* Goldens are for the default beta pair; make test EXTRA_CFLAGS=-DC2M_POSTFILTER_PDF skips amplitude. */
#ifdef C2M_POSTFILTER_PDF
    printf("amplitude comparison skipped: libc2fx implements only the default post-filter pair\n");
#endif
    if (dp == NULL) {
        fprintf(stderr, "cannot open vectors (run from the repository root)\n");
        return 1;
    }
    while ((ent = readdir(dp)) != NULL) {
        if (!ends_with(ent->d_name, ".c2")) {
            continue;
        }
        if (count >= MAX_SETS || strlen(ent->d_name) >= NAME_LEN) {
            fprintf(stderr, "too many bitstreams in vectors\n");
            closedir(dp);
            return 1;
        }
        memcpy(names[count], ent->d_name, strlen(ent->d_name) + 1);
        count += 1;
    }
    closedir(dp);
    if (count == 0) {
        fprintf(stderr, "no .c2 files in vectors\n");
        return 1;
    }
    qsort(names, (size_t)count, NAME_LEN, name_less);
    for (int s = 0; s < count; s++) {
        failed |= run_file(names[s]);
    }
    printf("sizeof c2fx_front %zu\n", sizeof(c2fx_front));
    return failed ? 1 : 0;
}

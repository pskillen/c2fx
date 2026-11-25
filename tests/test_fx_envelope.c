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

static int check_energy_table(void)
{
    c2fx_front fx;
    c2fx_indices idx;
    c2fx_subframe out[2];

    for (int q = 0; q < 63; q++) {
        double db = -10.0 + (double)q * (25.0 / 32.0);
        int expect = (int)llround(log2(pow(10.0, db / 10.0)) * 2048.0);

        memset(&idx, 0, sizeof idx);
        c2fx_front_init(&fx);
        if ((q & 1) != 0) {
            fx.prev_e = (uint8_t)(q / 2);
            idx.e = (uint8_t)(q - q / 2);
            c2fx_dequant_frame(&fx, &idx, out);
            if (out[0].energy_log2_q11 != (int16_t)expect) {
                fprintf(stderr, "energy table q1=%d: got %d expect %d\n", q,
                        (int)out[0].energy_log2_q11, expect);
                return 1;
            }
        } else {
            idx.e = (uint8_t)(q / 2);
            c2fx_dequant_frame(&fx, &idx, out);
            if (out[1].energy_log2_q11 != (int16_t)expect) {
                fprintf(stderr, "energy table q1=%d: got %d expect %d\n", q,
                        (int)out[1].energy_log2_q11, expect);
                return 1;
            }
        }
    }
    return 0;
}

static int check_sum_eps(void)
{
    const int32_t v[4] = { -28000, -28000, -28000, -28000 };
    const int32_t got = c2fx_log2_sum(v, 4, C2FX_SUM_EPS_LOG2_Q11);
    const int32_t wrong_eps = c2fx_log2_sum(v, 4, -(64 << 11));

    if (got == wrong_eps) {
        fprintf(stderr, "sum eps probe did not distinguish constants\n");
        return 1;
    }
    if (got != -23070) {
        fprintf(stderr, "sum eps golden %d got %d\n", -23070, (int)got);
        return 1;
    }
    return 0;
}

static void envelope(c2fx_subframe *sub)
{
    int32_t log_den[256];
    int32_t log_num[256];
    int32_t log_p[256];

    c2fx_envelope(sub, log_den, log_num, log_p);
}

#ifndef C2M_POSTFILTER_PDF
/* Harmonic amplitudes against the float model. The reference LPC is the
 * double LSP to LPC. worst is the largest error seen so far, in dB.
 */
static int harmonics_over_bar(const c2m_subframe *ms, c2fx_subframe *fs, double *worst)
{
    double a[C2M_LPC_ORDER + 1];
    c2m_envelope env;
    const double db_per_q11 = 20.0 * log10(2.0) / 2048.0;

    c2m_lsp_to_lpc(ms->lsp_rad, a);
    c2m_envelope_sample(a, ms->w0, ms->energy_db, &env);
    envelope(fs);
    if (fs->count != env.count) {
        fprintf(stderr, "harmonic count fx %d model %d\n", fs->count, env.count);
        return 1;
    }
    for (int m = 0; m < env.count; m++) {
        double err;

        if (env.amp[m] <= 0.0) {
            err = (fs->amp_log2_q11[m] == (int16_t)-32768) ? 0.0 : 100.0;
        } else {
            double model_q11 = log2(env.amp[m]) * 2048.0;

            err = fabs((double)fs->amp_log2_q11[m] - model_q11) * db_per_q11;
        }
        if (err > *worst) {
            *worst = err;
        }
    }
    return 0;
}
#endif

/* Voiced frame whose running LSP sum is exactly 4000 Hz (q1 8000). */
static void set_frame_4000(c2fx_indices *fidx, c2m_indices *midx)
{
    static const int d[C2FX_LSP_ORDER] = {31, 31, 31, 31, 7, 10, 10, 10, 10, 10};

    memset(fidx, 0, sizeof *fidx);
    memset(midx, 0, sizeof *midx);
    fidx->v_a = 1;
    fidx->v_b = 1;
    fidx->i = 60;
    fidx->e = 20;
    midx->v_a = 1;
    midx->v_b = 1;
    midx->i = 60;
    midx->e = 20;
    for (int j = 0; j < C2FX_LSP_ORDER; j++) {
        fidx->d[j] = d[j];
        midx->d[j] = d[j];
    }
}

static int check_lsp_4000(void)
{
    c2fx_indices fidx;
    c2m_indices midx;
    c2fx_front fx;
    c2m_decode md;
    c2fx_subframe fs[2];
    c2m_subframe ms[2];
    int seen = 0;

    set_frame_4000(&fidx, &midx);
    c2fx_front_init(&fx);
    c2m_decode_init(&md);
    c2fx_dequant_frame(&fx, &fidx, fs);
    c2m_decode_frame(&md, &midx, ms);
    for (int j = 0; j < C2FX_LSP_ORDER; j++) {
        if (fs[1].lsp_q1[j] == 8000) {
            seen = 1;
        }
    }
    if (!seen) {
        fprintf(stderr, "4000 Hz frame did not decode an LSP at 8000 q1\n");
        return 1;
    }
#ifndef C2M_POSTFILTER_PDF
    {
        double worst = 0.0;

        if (harmonics_over_bar(&ms[0], &fs[0], &worst) != 0 ||
            harmonics_over_bar(&ms[1], &fs[1], &worst) != 0 || worst > AMP_DB) {
            fprintf(stderr, "4000 Hz frame worst %.3f dB\n", worst);
            return 1;
        }
        printf("lsp 4000 Hz frame worst %7.3f dB\n", worst);
    }
#else
    envelope(&fs[0]);
    envelope(&fs[1]);
    printf("lsp 4000 Hz frame decoded\n");
#endif
    return 0;
}

static double poly_mag2(const double *a, double angle)
{
    double re = 0.0;
    double im = 0.0;

    for (int k = 0; k <= C2FX_LSP_ORDER; k++) {
        double phase = angle * (double)k;

        re += a[k] * cos(phase);
        im -= a[k] * sin(phase);
    }
    return re * re + im * im;
}

/* |A| of c2fx_lsp_to_lpc against the double LSP to LPC, in dB.
 * C2M_POWER_FLOOR keeps a shared null from dominating the ratio.
 */
static double lpc_db(const int16_t q1[C2FX_LSP_ORDER])
{
    int32_t wide[C2FX_LSP_ORDER + 1];
    int32_t quad[3 * (C2FX_LSP_ORDER + 3)];
    int q;
    double hz[C2FX_LSP_ORDER];
    double rad[C2FX_LSP_ORDER];
    double a[C2M_LPC_ORDER + 1];
    double afx[C2FX_LSP_ORDER + 1];
    double worst = 0.0;

    for (int j = 0; j < C2FX_LSP_ORDER; j++) {
        hz[j] = (double)q1[j] / 2.0;
    }
    c2m_lsp_hz_to_rad(hz, rad);
    c2m_lsp_to_lpc(rad, a);
    c2fx_lsp_to_lpc(q1, wide, &q, quad);
    for (int k = 0; k <= C2FX_LSP_ORDER; k++) {
        afx[k] = ldexp((double)wide[k], -q);
    }
    for (int bin = 0; bin < C2M_SPECTRUM_BINS; bin++) {
        double angle = 2.0 * C2M_PI * (double)bin / (double)C2M_DFT_SIZE;
        double md = poly_mag2(a, angle);
        double mf = poly_mag2(afx, angle);
        double db = fabs(10.0 * log10((mf + C2M_POWER_FLOOR) / (md + C2M_POWER_FLOOR)));

        if (db > worst) {
            worst = db;
        }
    }
    return worst;
}

/* Every table frequency 0, 25, ..., 4000 Hz as the single LSP value
 * (all ten LSPs), against the double LSP to LPC. The bar is AMP_DB.
 */
static int check_lsp_grid(void)
{
    double worst = 0.0;

    for (int hz = 0; hz <= 4000; hz += 25) {
        int16_t q1[C2FX_LSP_ORDER];
        double db;

        for (int j = 0; j < C2FX_LSP_ORDER; j++) {
            q1[j] = (int16_t)(hz * 2);
        }
        db = lpc_db(q1);
        if (db > worst) {
            worst = db;
        }
    }
    printf("lsp grid 0..4000 Hz worst %7.3f dB\n", worst);
    if (worst > AMP_DB) {
        fprintf(stderr, "lsp grid worst %.3f dB\n", worst);
        return 1;
    }
    return 0;
}

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
            envelope(&fs[s]);
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
#ifdef C2M_POSTFILTER_PDF
    printf("amplitude comparison skipped: libc2fx implements only the default post-filter pair\n");
#endif
    if (dp == NULL) {
        fprintf(stderr, "cannot open vectors (run from the repository root)\n");
        return 1;
    }
    failed |= check_energy_table();
    failed |= check_sum_eps();
    failed |= check_lsp_4000();
    failed |= check_lsp_grid();
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

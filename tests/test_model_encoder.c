// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The c2fx Authors

#define _POSIX_C_SOURCE 200809L

#include "c2m_analysis.h"
#include "c2m_encode.h"
#include "c2m_lpc.h"
#include "test_lsd_helper.h"

#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Spec 10.3. Voice: voicing, energy and input LSD are asserted; w0, octave and
 * LSP-tail columns are report-only (provisional until the owner's listening test).
 * Signal files are measured only. Energy is not asserted on the six OQ-10 files.
 */

#define MAX_SETS 64
#define NAME_LEN 256

typedef struct c2m_tally {
    int w_ok;
    int w_n;
    int oct;
    int v_ok;
    int v_bits;
    int e_ok;
    int e_n;
    double lsp_sum;
    int lsp_over;
    int lsp_n;
} c2m_tally;

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

static int energy_skipped(const char *base)
{
    return strcmp(base, "sine_100hz") == 0 || strcmp(base, "sine_150hz") == 0 ||
        strcmp(base, "sine_200hz") == 0 || strcmp(base, "sine_300hz") == 0 ||
        strcmp(base, "sine_400hz") == 0 || strcmp(base, "chirp_80_400hz") == 0;
}

static int16_t *read_pcm(const char *path, int *n_out)
{
    FILE *in = fopen(path, "rb");
    long bytes;
    uint8_t *raw;
    int16_t *pcm;
    int n;

    if (in == NULL) {
        return NULL;
    }
    if (fseek(in, 0, SEEK_END) != 0 || (bytes = ftell(in)) < 0 || fseek(in, 0, SEEK_SET) != 0) {
        fclose(in);
        return NULL;
    }
    if ((bytes % 2) != 0) {
        fclose(in);
        return NULL;
    }
    n = (int)(bytes / 2);
    raw = malloc((size_t)bytes == 0 ? 1 : (size_t)bytes);
    pcm = malloc((size_t)n * sizeof pcm[0] == 0 ? 1 : (size_t)n * sizeof pcm[0]);
    if (raw == NULL || pcm == NULL ||
        (bytes > 0 && fread(raw, 1, (size_t)bytes, in) != (size_t)bytes)) {
        free(raw);
        free(pcm);
        fclose(in);
        return NULL;
    }
    fclose(in);
    for (int i = 0; i < n; i++) {
        uint16_t u = (uint16_t)raw[2 * i] | ((uint16_t)raw[2 * i + 1] << 8);

        pcm[i] = (int16_t)u;
    }
    free(raw);
    *n_out = n;
    return pcm;
}

static uint8_t *read_file(const char *path, size_t *n_out)
{
    FILE *in = fopen(path, "rb");
    long bytes;
    uint8_t *buf;

    if (in == NULL) {
        return NULL;
    }
    if (fseek(in, 0, SEEK_END) != 0 || (bytes = ftell(in)) < 0 || fseek(in, 0, SEEK_SET) != 0) {
        fclose(in);
        return NULL;
    }
    buf = malloc((size_t)bytes == 0 ? 1 : (size_t)bytes);
    if (buf == NULL || (bytes > 0 && fread(buf, 1, (size_t)bytes, in) != (size_t)bytes)) {
        free(buf);
        fclose(in);
        return NULL;
    }
    fclose(in);
    *n_out = (size_t)bytes;
    return buf;
}

static double envelope_db(const double a[C2M_LPC_ORDER + 1], double f_hz)
{
    double ang = 2.0 * C2M_PI * f_hz / C2M_FS_HZ;
    double re = 0.0;
    double im = 0.0;
    double mag;

    for (int k = 0; k <= C2M_LPC_ORDER; k++) {
        re += a[k] * cos(ang * (double)k);
        im -= a[k] * sin(ang * (double)k);
    }
    mag = hypot(re, im);
    if (mag < 1e-30) {
        mag = 1e-30;
    }
    return -20.0 * log10(mag);
}

static double lsp_distance(const c2m_indices *left, const c2m_indices *right)
{
    c2m_params a;
    c2m_params b;
    double aa[C2M_LPC_ORDER + 1];
    double bb[C2M_LPC_ORDER + 1];
    double acc = 0.0;
    int n = 0;

    c2m_dequant(left, &a);
    c2m_dequant(right, &b);
    c2m_lsp_to_lpc(a.lsp_rad, aa);
    c2m_lsp_to_lpc(b.lsp_rad, bb);
    for (double f = 100.0; f <= 3800.0 + 1e-9; f += 10.0) {
        double diff = envelope_db(aa, f) - envelope_db(bb, f);

        acc += diff * diff;
        n += 1;
    }
    return sqrt(acc / (double)n);
}

static void add_tally(c2m_tally *sum, const c2m_tally *one, int count_energy)
{
    sum->w_ok += one->w_ok;
    sum->w_n += one->w_n;
    sum->oct += one->oct;
    sum->v_ok += one->v_ok;
    sum->v_bits += one->v_bits;
    if (count_energy) {
        sum->e_ok += one->e_ok;
        sum->e_n += one->e_n;
    }
    sum->lsp_sum += one->lsp_sum;
    sum->lsp_over += one->lsp_over;
    sum->lsp_n += one->lsp_n;
}

static void format_pct(char *buf, size_t n, int num, int den)
{
    if (den == 0) {
        snprintf(buf, n, "n/a");
        return;
    }
    snprintf(buf, n, "%.1f%%", 100.0 * (double)num / (double)den);
}

static int at_least(int num, int den, int percent)
{
    return num * 100 >= den * percent;
}

static int check_pcm(const char *dir, const char *pcm_name, int voice, c2m_tally *pool)
{
    char base[NAME_LEN];
    char path[512];
    int16_t *pcm = NULL;
    uint8_t *ours = NULL;
    uint8_t *ref = NULL;
    size_t ref_n = 0;
    int n = 0;
    int frames;
    int ref_frames;
    int failed = 0;
    int count_energy;
    c2m_tally tally;
    char wbuf[16];
    char vbuf[16];
    char ebuf[16];
    char obuf[16];
    char lspbuf[16];
    int16_t *dec_ours = NULL;
    int16_t *dec_ref = NULL;
    long dec_count = 0;
    int lsd_frames = 0;
    double lsd_ref = -1.0;
    double lsd_ours = -1.0;

    memset(&tally, 0, sizeof tally);
    if (!ends_with(pcm_name, ".pcm")) {
        return 1;
    }
    if (strlen(pcm_name) - 4 >= NAME_LEN) {
        fprintf(stderr, "name too long: %s\n", pcm_name);
        return 1;
    }
    memcpy(base, pcm_name, strlen(pcm_name) - 4);
    base[strlen(pcm_name) - 4] = '\0';
    count_energy = !energy_skipped(base);
    snprintf(path, sizeof path, "%s/%s", dir, pcm_name);
    pcm = read_pcm(path, &n);
    snprintf(path, sizeof path, "vectors/%s.c2", base);
    ref = read_file(path, &ref_n);
    if (pcm == NULL || ref == NULL) {
        fprintf(stderr, "cannot read %s\n", base);
        free(pcm);
        free(ref);
        return 1;
    }
    if (n == 0 || (n % (2 * C2M_ANALYSIS_HOP)) != 0) {
        fprintf(stderr, "%s is not a whole number of frames\n", base);
        free(pcm);
        free(ref);
        return 1;
    }
    frames = n / (2 * C2M_ANALYSIS_HOP);
    ref_frames = (int)(ref_n / C2M_FRAME_BYTES);
    if (ref_frames != frames || ref_n != (size_t)frames * C2M_FRAME_BYTES) {
        fprintf(stderr, "%s has %d frames of audio and %d reference frames\n", base, frames, ref_frames);
        free(pcm);
        free(ref);
        return 1;
    }
    ours = malloc((size_t)frames * C2M_FRAME_BYTES);
    if (ours == NULL || c2m_encode(pcm, n, ours) != frames) {
        fprintf(stderr, "%s encode failed\n", base);
        free(ours);
        free(pcm);
        free(ref);
        return 1;
    }
    for (int k = 0; k < frames; k++) {
        c2m_indices got;
        c2m_indices expect;
        double dist;

        c2m_frame_unpack(ours + (size_t)k * C2M_FRAME_BYTES, &got);
        c2m_frame_unpack(ref + (size_t)k * C2M_FRAME_BYTES, &expect);
        tally.v_bits += 2;
        tally.v_ok += (got.v_a == expect.v_a) + (got.v_b == expect.v_b);
        tally.e_n += 1;
        if (abs(got.e - expect.e) <= 1) {
            tally.e_ok += 1;
        }
        if (got.v_b && expect.v_b) {
            double f_got = c2m_f0_hz(got.i);
            double f_ref = c2m_f0_hz(expect.i);

            tally.w_n += 1;
            if (abs(got.i - expect.i) <= 2) {
                tally.w_ok += 1;
            }
            if (f_got > 0.0 && f_ref > 0.0 && (f_got > f_ref ? f_got / f_ref : f_ref / f_got) > sqrt(2.0)) {
                tally.oct += 1;
            }
        }
        dist = lsp_distance(&got, &expect);
        tally.lsp_sum += dist;
        tally.lsp_n += 1;
        if (dist > 4.0) {
            tally.lsp_over += 1;
        }
    }
    format_pct(wbuf, sizeof wbuf, tally.w_ok, tally.w_n);
    format_pct(vbuf, sizeof vbuf, tally.v_ok, tally.v_bits);
    format_pct(ebuf, sizeof ebuf, tally.e_ok, tally.e_n);
    format_pct(obuf, sizeof obuf, tally.oct, tally.w_n);
    if (tally.lsp_n == 0) {
        snprintf(lspbuf, sizeof lspbuf, "n/a");
    } else {
        snprintf(lspbuf, sizeof lspbuf, "%.2f", 100.0 * (double)tally.lsp_over / (double)tally.lsp_n);
    }
    if (voice) {
        if (c2m_test_decode_c2(ours, (size_t)frames * C2M_FRAME_BYTES, &dec_ours, &dec_count) != 0 ||
            c2m_test_decode_c2(ref, ref_n, &dec_ref, &dec_count) != 0) {
            fprintf(stderr, "%s decode for LSD failed\n", base);
            failed = 1;
        } else if (dec_count != (long)n) {
            fprintf(stderr, "%s decoded sample count mismatch\n", base);
            failed = 1;
        } else {
            lsd_ref = c2m_test_mean_lsd(dec_ref, pcm, pcm, dec_count, &lsd_frames);
            lsd_ours = c2m_test_mean_lsd(dec_ours, pcm, pcm, dec_count, &lsd_frames);
            if (lsd_frames == 0) {
                fprintf(stderr, "%s has no voiced frames for input LSD\n", base);
                failed = 1;
            } else if (lsd_ours > lsd_ref + C2M_TEST_INPUT_LSD_MARGIN_DB) {
                fprintf(
                    stderr,
                    "%s LSD vs input %.3f dB exceeds reference %.3f dB + %.1f\n",
                    base,
                    lsd_ours,
                    lsd_ref,
                    C2M_TEST_INPUT_LSD_MARGIN_DB
                );
                failed = 1;
            }
        }
    }
    {
        char lsd_ref_buf[16];
        char lsd_ours_buf[16];
        char margin_buf[16];

        if (voice && lsd_frames > 0) {
            snprintf(lsd_ref_buf, sizeof lsd_ref_buf, "%.3f", lsd_ref);
            snprintf(lsd_ours_buf, sizeof lsd_ours_buf, "%.3f", lsd_ours);
            snprintf(
                margin_buf,
                sizeof margin_buf,
                "%.3f",
                lsd_ref + C2M_TEST_INPUT_LSD_MARGIN_DB - lsd_ours
            );
        } else {
            snprintf(lsd_ref_buf, sizeof lsd_ref_buf, "n/a");
            snprintf(lsd_ours_buf, sizeof lsd_ours_buf, "n/a");
            snprintf(margin_buf, sizeof margin_buf, "n/a");
        }
        printf(
            "%-22s %8s %8s %8s %8.2f %8s %8s %8s %8s %8s\n",
            base,
            wbuf,
            vbuf,
            ebuf,
            tally.lsp_n == 0 ? 0.0 : tally.lsp_sum / (double)tally.lsp_n,
            lspbuf,
            obuf,
            lsd_ref_buf,
            lsd_ours_buf,
            margin_buf
        );
    }
    if (voice) {
        if (!at_least(tally.v_ok, tally.v_bits, 90)) {
            fprintf(stderr, "%s voicing agreement is %d/%d, bar 90%%\n", base, tally.v_ok, tally.v_bits);
            failed = 1;
        }
        if (count_energy && !at_least(tally.e_ok, tally.e_n, 95)) {
            fprintf(stderr, "%s energy within +-1 is %d/%d, bar 95%%\n", base, tally.e_ok, tally.e_n);
            failed = 1;
        }
    }
    add_tally(pool, &tally, count_energy);
    free(dec_ours);
    free(dec_ref);
    free(ours);
    free(pcm);
    free(ref);
    return failed;
}

static int scan_dir(const char *dir, int voice, int expect_count, c2m_tally *pool)
{
    DIR *dp = opendir(dir);
    struct dirent *ent;
    char names[MAX_SETS][NAME_LEN];
    int count = 0;
    int failed = 0;

    if (dp == NULL) {
        fprintf(stderr, "cannot open %s (run from the repository root)\n", dir);
        return 1;
    }
    while ((ent = readdir(dp)) != NULL) {
        if (!ends_with(ent->d_name, ".pcm")) {
            continue;
        }
        if (count >= MAX_SETS || strlen(ent->d_name) >= NAME_LEN) {
            fprintf(stderr, "too many pcm files in %s\n", dir);
            closedir(dp);
            return 1;
        }
        memcpy(names[count], ent->d_name, strlen(ent->d_name) + 1);
        count += 1;
    }
    closedir(dp);
    qsort(names, (size_t)count, NAME_LEN, name_less);
    for (int i = 0; i < count; i++) {
        if (check_pcm(dir, names[i], voice, pool) != 0) {
            failed = 1;
        }
    }
    if (count != expect_count) {
        fprintf(stderr, "expected %d pcm files in %s, found %d\n", expect_count, dir, count);
        failed = 1;
    }
    return failed;
}

int main(void)
{
    c2m_tally pool;
    char wbuf[16];
    char vbuf[16];
    char ebuf[16];
    char obuf[16];
    char lspbuf[16];
    int failed;

    memset(&pool, 0, sizeof pool);
    /* w0, octave and LSP-tail bars are report-only until the owner's listening test. */
    printf(
        "%-22s %8s %8s %8s %8s %8s %8s %8s %8s %8s\n",
        "file",
        "w0",
        "voicing",
        "energy",
        "lsp_dB",
        "lsp>4%",
        "octave",
        "lsd_ref",
        "lsd_ours",
        "margin"
    );
    failed = scan_dir("tests/data/voice", 1, 4, &pool);
    if (scan_dir("tests/data/signals", 0, 10, &pool) != 0) {
        failed = 1;
    }
    format_pct(wbuf, sizeof wbuf, pool.w_ok, pool.w_n);
    format_pct(vbuf, sizeof vbuf, pool.v_ok, pool.v_bits);
    format_pct(ebuf, sizeof ebuf, pool.e_ok, pool.e_n);
    format_pct(obuf, sizeof obuf, pool.oct, pool.w_n);
    if (pool.lsp_n == 0) {
        snprintf(lspbuf, sizeof lspbuf, "n/a");
    } else {
        snprintf(lspbuf, sizeof lspbuf, "%.2f", 100.0 * (double)pool.lsp_over / (double)pool.lsp_n);
    }
    printf(
        "%-22s %8s %8s %8s %8.2f %8s %8s %8s %8s %8s\n",
        "pooled",
        wbuf,
        vbuf,
        ebuf,
        pool.lsp_n == 0 ? 0.0 : pool.lsp_sum / (double)pool.lsp_n,
        lspbuf,
        obuf,
        "n/a",
        "n/a",
        "n/a"
    );
    return failed ? 1 : 0;
}

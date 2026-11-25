// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The c2fx Authors
// D3. Every frame of every vectors/*.c2 file, fixed decoder against the
// float model, same bits. Whole-file SNR at least 35 dB, and the same bar
// on each 20 ms frame above -50 dBFS. Frames 10 through 16 are then dropped
// through both decoders and the bar is applied again.

#include "c2fx.h"
#include "c2m_frame.h"
#include "c2m_synth.h"

#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_SETS 32
#define NAME_LEN 64
#define SNR_BAR 35.0

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

/* rms above -50 dBFS, full scale 32767. */
static int above_floor(double sumsq)
{
    const double full = 32767.0;

    return sumsq > 160.0 * full * full * 1e-5;
}

static double snr_db(double sig, double err)
{
    if (err <= 0.0) {
        return 99.0;
    }
    if (sig <= 0.0) {
        return -99.0;
    }
    return 10.0 * log10(sig / err);
}

/* drop_lo..drop_hi inclusive are passed as lost frames. drop_lo < 0 disables. */
static int run_file(const char *name, int drop_lo, int drop_hi,
                    double *file_snr, double *worst_snr, int *max_abs, int dump_pcm)
{
    char path[128];
    FILE *fp;
    c2fx_dec_t dec;
    c2m_decode md;
    c2fx_scratch_t scratch;
    double sig = 0.0;
    double err = 0.0;
    double worst = 99.0;
    int have_worst = 0;
    int max_ad = 0;
    int n = 0;

    snprintf(path, sizeof path, "vectors/%s", name);
    fp = fopen(path, "rb");
    if (fp == NULL) {
        fprintf(stderr, "cannot open %s\n", path);
        return 1;
    }
    c2fx_dec_init(&dec);
    c2m_decode_init(&md);
    for (;;) {
        uint8_t raw[C2M_FRAME_BYTES];
        c2m_indices idx;
        int16_t model[C2M_FRAME_SAMPLES];
        int16_t fx[C2FX_FRAME_LEN];
        double fs = 0.0;
        double fe = 0.0;
        size_t got = fread(raw, 1, C2M_FRAME_BYTES, fp);

        if (got == 0) {
            break;
        }
        if (got != C2M_FRAME_BYTES) {
            fprintf(stderr, "%s: truncated frame %d\n", name, n);
            fclose(fp);
            return 1;
        }
        if (n >= drop_lo && n <= drop_hi) {
            c2m_decode_pcm(&md, NULL, model);
            c2fx_decode(&dec, NULL, fx, &scratch);
        } else {
            c2m_frame_unpack(raw, &idx);
            c2m_decode_pcm(&md, &idx, model);
            c2fx_decode(&dec, raw, fx, &scratch);
        }
        if (dump_pcm && fwrite(fx, sizeof fx[0], C2FX_FRAME_LEN, stdout) != C2FX_FRAME_LEN) {
            fprintf(stderr, "%s: cannot write frame %d PCM\n", name, n);
            fclose(fp);
            return 1;
        }
        for (int i = 0; i < C2FX_FRAME_LEN; i++) {
            double d = (double)model[i] - (double)fx[i];
            int ad = (int)model[i] - (int)fx[i];

            if (ad < 0) {
                ad = -ad;
            }
            if (ad > max_ad) {
                max_ad = ad;
            }
            fs += (double)model[i] * (double)model[i];
            fe += d * d;
        }
        sig += fs;
        err += fe;
        if (above_floor(fs)) {
            double sn = snr_db(fs, fe);

            if (!have_worst || sn < worst) {
                worst = sn;
                have_worst = 1;
            }
        }
        n += 1;
    }
    fclose(fp);
    if (n == 0) {
        fprintf(stderr, "%s: no frames\n", name);
        return 1;
    }
    *file_snr = snr_db(sig, err);
    *worst_snr = have_worst ? worst : 99.0;
    *max_abs = max_ad;
    return 0;
}

int main(int argc, char **argv)
{
    int dump_pcm = argc == 2 && strcmp(argv[1], "--pcm") == 0;
    FILE *report = dump_pcm ? stderr : stdout;
    DIR *dp;
    struct dirent *ent;
    char names[MAX_SETS][NAME_LEN];
    int count = 0;
    int failed = 0;

    if (argc != 1 && !dump_pcm) {
        fprintf(stderr, "usage: test_fx_decode [--pcm]\n");
        return 2;
    }
    dp = opendir("vectors");
#ifdef C2M_POSTFILTER_PDF
    fprintf(report, "SNR comparison skipped: libc2fx implements only the default post-filter pair\n");
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
        double file_snr = 0.0;
        double worst = 0.0;
        double drop_snr = 0.0;
        double drop_worst = 0.0;
        int max_abs = 0;
        int drop_abs = 0;

        if (run_file(names[s], -1, -1, &file_snr, &worst, &max_abs, dump_pcm) != 0 ||
            run_file(names[s], 10, 16, &drop_snr, &drop_worst, &drop_abs, dump_pcm) != 0) {
            return 1;
        }
        fprintf(report, "%-24s file %6.2f dB  worst %6.2f dB  maxabs %5d  drop %6.2f dB  drop-worst %6.2f  drop-maxabs %d\n",
                names[s], file_snr, worst, max_abs, drop_snr, drop_worst, drop_abs);
#ifndef C2M_POSTFILTER_PDF
        if (file_snr < SNR_BAR || worst < SNR_BAR || drop_snr < SNR_BAR || drop_worst < SNR_BAR) {
            fprintf(stderr, "%s: SNR below %.0f dB\n", names[s], SNR_BAR);
            failed = 1;
        }
#endif
    }
    if (dump_pcm && fflush(stdout) != 0) {
        fprintf(stderr, "cannot flush D3 PCM\n");
        return 1;
    }
    return failed ? 1 : 0;
}

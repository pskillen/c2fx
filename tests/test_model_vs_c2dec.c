// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The c2fx Authors

#define _POSIX_C_SOURCE 200809L

#include "c2m_synth.h"

#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_SETS 64
#define NAME_LEN 256
#define LSD_BINS 127

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

static int is_voice_set(const char *base)
{
    return strcmp(base, "high_voice") == 0 || strcmp(base, "low_voice") == 0 ||
           strcmp(base, "test_transmission") == 0 || strcmp(base, "ten_second_countdown") == 0;
}

static int16_t read_le(const uint8_t *p)
{
    return (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static int load_pair(
    const char *c2_path,
    const char *pcm_path,
    int16_t **model_out,
    int16_t **ref_out,
    long *count_out
)
{
    FILE *bits = fopen(c2_path, "rb");
    FILE *ref = fopen(pcm_path, "rb");
    c2m_decode state;
    int16_t *model;
    int16_t *gold;
    long cap = 0;
    long count = 0;

    if (bits == NULL || ref == NULL) {
        fprintf(stderr, "missing %s or %s\n", c2_path, pcm_path);
        if (bits != NULL) {
            fclose(bits);
        }
        if (ref != NULL) {
            fclose(ref);
        }
        return 1;
    }
    if (fseek(ref, 0, SEEK_END) != 0) {
        fclose(bits);
        fclose(ref);
        return 1;
    }
    cap = ftell(ref) / (long)sizeof(int16_t);
    if (fseek(ref, 0, SEEK_SET) != 0 || cap <= 0) {
        fclose(bits);
        fclose(ref);
        return 1;
    }
    model = calloc((size_t)cap, sizeof(int16_t));
    gold = calloc((size_t)cap, sizeof(int16_t));
    if (model == NULL || gold == NULL) {
        free(model);
        free(gold);
        fclose(bits);
        fclose(ref);
        return 1;
    }
    c2m_decode_init(&state);
    while (count + C2M_FRAME_SAMPLES <= cap) {
        uint8_t raw[C2M_FRAME_BYTES];
        uint8_t pcm[C2M_FRAME_SAMPLES * 2];
        c2m_indices idx;
        int16_t decoded[C2M_FRAME_SAMPLES];
        size_t got_bits = fread(raw, 1, C2M_FRAME_BYTES, bits);
        size_t got_pcm;

        if (got_bits == 0) {
            break;
        }
        if (got_bits != C2M_FRAME_BYTES) {
            fprintf(stderr, "truncated bitstream %s\n", c2_path);
            free(model);
            free(gold);
            fclose(bits);
            fclose(ref);
            return 1;
        }
        got_pcm = fread(pcm, 1, sizeof pcm, ref);
        if (got_pcm != sizeof pcm) {
            fprintf(stderr, "truncated reference %s\n", pcm_path);
            free(model);
            free(gold);
            fclose(bits);
            fclose(ref);
            return 1;
        }
        c2m_frame_unpack(raw, &idx);
        c2m_decode_pcm(&state, &idx, decoded);
        for (int n = 0; n < C2M_FRAME_SAMPLES; n++) {
            model[count] = decoded[n];
            gold[count] = read_le(pcm + 2 * n);
            count += 1;
        }
    }
    fclose(bits);
    fclose(ref);
    *model_out = model;
    *ref_out = gold;
    *count_out = count;
    return 0;
}

static void frame_mag(const int16_t *pcm, double mag[LSD_BINS + 1])
{
    double windowed[256];

    for (int n = 0; n < 256; n++) {
        windowed[n] = 0.0;
    }
    for (int n = 0; n < C2M_FRAME_SAMPLES; n++) {
        double w = 0.5 * (1.0 - cos(2.0 * C2M_PI * (double)n / (double)C2M_FRAME_SAMPLES));

        windowed[n] = w * (double)pcm[n];
    }
    for (int k = 1; k <= LSD_BINS; k++) {
        double wr = 0.0;
        double wi = 0.0;
        double ang = 2.0 * C2M_PI * (double)k / 256.0;

        for (int n = 0; n < 256; n++) {
            wr += windowed[n] * cos(ang * (double)n);
            wi -= windowed[n] * sin(ang * (double)n);
        }
        mag[k] = sqrt(wr * wr + wi * wi);
    }
}

static int check_set(const char *dir, const char *c2_name, int *stop_lsd)
{
    char base[NAME_LEN];
    char c2_path[512];
    char pcm_path[512];
    int16_t *model = NULL;
    int16_t *ref = NULL;
    long count = 0;
    size_t base_len;
    double sum_m = 0.0;
    double sum_r = 0.0;
    double peak = 0.0;
    double level_db;
    double lsd_sum = 0.0;
    int used = 0;
    int voice;

    base_len = strlen(c2_name) - 3;
    if (base_len >= NAME_LEN) {
        return 1;
    }
    memcpy(base, c2_name, base_len);
    base[base_len] = '\0';
    voice = is_voice_set(base);
    snprintf(c2_path, sizeof c2_path, "%s/%s", dir, c2_name);
    snprintf(pcm_path, sizeof pcm_path, "%s/%s_c2dec.pcm", dir, base);
    if (load_pair(c2_path, pcm_path, &model, &ref, &count) != 0) {
        return 1;
    }
    for (long i = 0; i < count; i++) {
        double a = fabs((double)ref[i]);

        sum_m += (double)model[i] * (double)model[i];
        sum_r += (double)ref[i] * (double)ref[i];
        if (a > peak) {
            peak = a;
        }
    }
    if (count == 0 || sum_r == 0.0 || sum_m == 0.0) {
        printf("%-22s  %8s  %8s\n", base, "n/a", "n/a");
        free(model);
        free(ref);
        return 0;
    }
    level_db = 20.0 * log10(sqrt(sum_m / (double)count) / sqrt(sum_r / (double)count));
    {
        double thresh = peak * peak * pow(10.0, -40.0 / 10.0);

        for (long off = 0; off + C2M_FRAME_SAMPLES <= count; off += C2M_FRAME_SAMPLES) {
            double energy = 0.0;
            double mag_m[LSD_BINS + 1];
            double mag_r[LSD_BINS + 1];
            double acc = 0.0;

            for (int n = 0; n < C2M_FRAME_SAMPLES; n++) {
                energy += (double)ref[off + n] * (double)ref[off + n];
            }
            if (energy / (double)C2M_FRAME_SAMPLES < thresh) {
                continue;
            }
            frame_mag(model + off, mag_m);
            frame_mag(ref + off, mag_r);
            for (int k = 1; k <= LSD_BINS; k++) {
                double left = mag_m[k] < 1.0 ? 1.0 : mag_m[k];
                double right = mag_r[k] < 1.0 ? 1.0 : mag_r[k];
                double diff = 20.0 * log10(left) - 20.0 * log10(right);

                acc += diff * diff;
            }
            lsd_sum += sqrt(acc / (double)LSD_BINS);
            used += 1;
        }
    }
    if (used == 0) {
        printf("%-22s  %8.3f  %8s\n", base, level_db, "n/a");
    } else {
        double lsd = lsd_sum / (double)used;

        printf("%-22s  %8.3f  %8.3f\n", base, level_db, lsd);
        if (voice && lsd > 6.0) {
            fprintf(stderr, "%s LSD %.3f dB is above 6\n", base, lsd);
            *stop_lsd = 1;
        }
    }
    free(model);
    free(ref);
    if (voice && fabs(level_db) > 1.5) {
        fprintf(stderr, "%s level %.3f dB is outside ±1.5\n", base, level_db);
        return 1;
    }
    return 0;
}

int main(void)
{
    const char *dir = "vectors";
    DIR *dp;
    struct dirent *ent;
    char names[MAX_SETS][NAME_LEN];
    int count = 0;
    int failed = 0;
    int stop_lsd = 0;

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
    printf("%-22s  %8s  %8s\n", "set", "level_dB", "lsd_dB");
    for (int i = 0; i < count; i++) {
        if (check_set(dir, names[i], &stop_lsd) != 0) {
            failed = 1;
        }
    }
    if (stop_lsd) {
        return 1;
    }
    return failed ? 1 : 0;
}

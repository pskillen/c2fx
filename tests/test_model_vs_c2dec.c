// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The c2fx Authors

#define _POSIX_C_SOURCE 200809L

#include "c2m_synth.h"
#include "test_lsd_helper.h"

#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static int load_input_pcm(const char *path, int16_t **out, long count)
{
    FILE *fp = fopen(path, "rb");
    int16_t *buf;
    size_t got;

    if (fp == NULL) {
        fprintf(stderr, "missing input %s\n", path);
        return 1;
    }
    buf = calloc((size_t)count, sizeof(int16_t));
    if (buf == NULL) {
        fclose(fp);
        return 1;
    }
    got = fread(buf, sizeof(int16_t), (size_t)count, fp);
    fclose(fp);
    if ((long)got != count) {
        fprintf(stderr, "truncated input %s\n", path);
        free(buf);
        return 1;
    }
    *out = buf;
    return 0;
}

static int check_set(const char *dir, const char *c2_name)
{
    char base[NAME_LEN];
    char c2_path[512];
    char pcm_path[512];
    char input_path[512];
    int16_t *model = NULL;
    int16_t *ref = NULL;
    int16_t *input = NULL;
    long count = 0;
    size_t base_len;
    double sum_m = 0.0;
    double sum_r = 0.0;
    int failed = 0;
    int voice;
    int lsd_c2dec_frames = 0;
    int lsd_input_frames = 0;
    double lsd_vs_c2dec;
    double lsd_c2dec_input = -1.0;
    double lsd_ours_input = -1.0;

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
        sum_m += (double)model[i] * (double)model[i];
        sum_r += (double)ref[i] * (double)ref[i];
    }
    if (count == 0 || sum_r == 0.0 || sum_m == 0.0) {
        printf(
            "%-22s  %8s  %8s  %8s  %8s\n",
            base,
            "n/a",
            "n/a",
            "n/a",
            "n/a"
        );
        free(model);
        free(ref);
        return 0;
    }
    {
        double level_db = 20.0 * log10(sqrt(sum_m / (double)count) / sqrt(sum_r / (double)count));

        lsd_vs_c2dec = c2m_test_mean_lsd(model, ref, ref, count, &lsd_c2dec_frames);
        if (voice) {
            snprintf(input_path, sizeof input_path, "tests/data/voice/%s.pcm", base);
            if (load_input_pcm(input_path, &input, count) != 0) {
                free(model);
                free(ref);
                return 1;
            }
            lsd_c2dec_input = c2m_test_mean_lsd(ref, input, input, count, &lsd_input_frames);
            lsd_ours_input = c2m_test_mean_lsd(model, input, input, count, &lsd_input_frames);
            if (lsd_input_frames == 0) {
                fprintf(stderr, "%s has no voiced frames for input LSD\n", base);
                failed = 1;
            } else if (lsd_ours_input > lsd_c2dec_input + C2M_TEST_INPUT_LSD_MARGIN_DB) {
                fprintf(
                    stderr,
                    "%s LSD vs input %.3f dB exceeds c2dec %.3f dB + %.1f\n",
                    base,
                    lsd_ours_input,
                    lsd_c2dec_input,
                    C2M_TEST_INPUT_LSD_MARGIN_DB
                );
                failed = 1;
            }
        }
        if (voice && lsd_c2dec_frames == 0) {
            fprintf(stderr, "%s has no voiced frames for c2dec LSD\n", base);
            failed = 1;
        }
        if (lsd_c2dec_frames == 0) {
            printf(
                "%-22s  %8.3f  %8s  %8s  %8s\n",
                base,
                level_db,
                "n/a",
                "n/a",
                "n/a"
            );
        } else if (voice) {
            printf(
                "%-22s  %8.3f  %8.3f  %8.3f  %8.3f\n",
                base,
                level_db,
                lsd_vs_c2dec,
                lsd_c2dec_input,
                lsd_ours_input
            );
        } else {
            printf("%-22s  %8.3f  %8.3f  %8s  %8s\n", base, level_db, lsd_vs_c2dec, "n/a", "n/a");
        }
        if (voice && fabs(level_db) > 1.5) {
            fprintf(stderr, "%s level %.3f dB is outside +/-1.5\n", base, level_db);
            failed = 1;
        }
        free(input);
        free(model);
        free(ref);
        return failed;
    }
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
    qsort(names, (size_t)count, NAME_LEN, name_less);
    printf(
        "%-22s  %8s  %8s  %8s  %8s\n",
        "set",
        "level_dB",
        "lsd_c2dec",
        "lsd_dec_in",
        "lsd_ours_in"
    );
    for (int i = 0; i < count; i++) {
        if (check_set(dir, names[i]) != 0) {
            failed = 1;
        }
    }
    return failed ? 1 : 0;
}

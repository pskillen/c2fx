// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The c2fx Authors

#include "c2fx_internal.h"
#include "c2m_encode.h"
#include "c2m_analysis.h"
#include "c2m_pitch.h"
#include "c2m_voicing.h"

#include <glob.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

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

#define HZ_ONE INT32_C(1048576)

typedef struct voicing_row {
    int base;
    c2fx_voicing_tilt_band tilt;
} voicing_row;

typedef struct voicing_stats {
    int snr_mismatch;
    int base_matches;
    int final_matches;
    double near_snr_error;
} voicing_stats;

enum { MODEL_PITCH, OWN_PITCH, PATHS };

static double snr_db(c2fx_voicing_energy e)
{
    if (e.signal == 0) {
        return C2M_VOICING_SNR_NONE_DB;
    }
    if (e.error == 0 || (double)e.signal / e.error > C2M_VOICING_SNR_MAX_RATIO) {
        return C2M_VOICING_SNR_MAX_DB;
    }
    return 10.0 * log10((double)e.signal / e.error);
}

static c2fx_voicing_tilt_band model_tilt_band(double tilt)
{
    if (tilt > C2M_VOICING_TILT_ACCEPT) {
        return C2FX_TILT_ACCEPT;
    }
    if (tilt > C2M_VOICING_TILT_BRIDGE) {
        return C2FX_TILT_BRIDGE;
    }
    return tilt > C2M_VOICING_TILT_REJECT ? C2FX_TILT_POSITIVE : C2FX_TILT_REJECT;
}

static int run_file(const char *path, int voice)
{
    int n;
    int16_t *pcm = read_pcm(path, &n);
    struct c2fx_enc_scratch scratch;
    voicing_stats stats[PATHS] = {{0}};
    voicing_row history[PATHS][3] = {{{0}}};
    double prev_model = 0.0;
    int32_t prev_fixed = 0;
    int tilt_mismatches = 0;
    int rows = 0;
    int failed = 0;

    if (pcm == NULL) {
        fprintf(stderr, "%s: cannot read whole PCM frames\n", path);
        return 1;
    }
    int frames = n / (2 * C2FX_HOP);
    uint8_t *packed = malloc((size_t)frames * C2M_FRAME_BYTES);

    if (packed == NULL || c2m_encode(pcm, n, packed) != frames) {
        fprintf(stderr, "%s: model encode failed\n", path);
        free(packed);
        free(pcm);
        return 1;
    }
    /* Match the model pitch track from startup through the last lookahead. */
    for (int center = -2 * C2FX_HOP; center <= n + C2FX_HOP; center += C2FX_HOP) {
        int16_t x[C2FX_PITCH_SPAN];
        int idx = rows % 3;
        double model_hz = c2m_pitch_hz(pcm, n, center, prev_model);
        double model_snr = c2m_voicing_snr_db(pcm, n, center, model_hz);
        double model_tilt = c2m_voicing_tilt(pcm, n, center);
        int model_base = c2m_voicing_base(model_snr, model_tilt);
        int32_t hz[PATHS];

        for (int j = 0; j < C2FX_PITCH_SPAN; j++) {
            int i = center - C2FX_PITCH_BEFORE + j;

            x[j] = i >= 0 && i < n ? pcm[i] : 0;
        }
        hz[OWN_PITCH] = c2fx_pitch_hz(x, prev_fixed, &scratch);
        hz[MODEL_PITCH] = (int32_t)lround(model_hz * HZ_ONE);
        c2fx_voicing_tilt_band tilt = c2fx_voicing_tilt(x + C2FX_PITCH_BEFORE - C2FX_ANALYSIS_CENTER, &scratch);

        tilt_mismatches += tilt != model_tilt_band(model_tilt);
        for (int p = 0; p < PATHS; p++) {
            c2fx_voicing_energy e = c2fx_voicing_snr(hz[p], &scratch);
            int pass = c2fx_voicing_snr_pass(e);
            int mismatch = pass != (model_snr > C2M_VOICING_SNR_DB);
            int base = c2fx_voicing_base(pass, tilt);

            history[p][idx] = (voicing_row){base, tilt};
            stats[p].base_matches += base == model_base;
            stats[p].snr_mismatch += mismatch;
            if (fabs(model_snr - C2M_VOICING_SNR_DB) <= 3.0) {
                stats[p].near_snr_error = fmax(stats[p].near_snr_error, fabs(snr_db(e) - model_snr));
            }
            if (voice && p == MODEL_PITCH && mismatch) {
                printf("%s center %d SNR decision differs (model %.6f dB, fixed %.6f dB)\n",
                       path, center, model_snr, snr_db(e));
            }
            /* Row r-1 is now ready. The model emits rows centered at -80
             * through n-160 inclusive, with the first row in bit 7.
             */
            int bit = center / C2FX_HOP;
            if (bit >= 0 && bit < n / C2FX_HOP) {
                voicing_row mid = history[p][(idx + 2) % 3];
                int final = c2fx_voicing_final(history[p][(idx + 1) % 3].base, mid.base, base, mid.tilt);
                int expected = (packed[bit / 2 * C2M_FRAME_BYTES] >> (7 - bit % 2)) & 1;

                stats[p].final_matches += final == expected;
            }
        }
        prev_model = model_base ? model_hz : 0.0;
        prev_fixed = history[OWN_PITCH][idx].base ? hz[OWN_PITCH] : 0;
        rows++;
    }
    for (int p = 0; p < PATHS; p++) {
        printf("%s %-5s %d rows, base %.4f%%, final %.4f%% (%d bits), near SNR max %.5f dB, SNR flips %d, tilt mismatches %d\n",
               path, p == MODEL_PITCH ? "model" : "own", rows, 100.0 * stats[p].base_matches / rows,
               100.0 * stats[p].final_matches / (n / C2FX_HOP), n / C2FX_HOP,
               stats[p].near_snr_error, stats[p].snr_mismatch, tilt_mismatches);
    }
    if (voice && INT64_C(100) * stats[MODEL_PITCH].snr_mismatch > INT64_C(2) * rows) {
        fprintf(stderr, "%s: SNR decision gate failed (%d/%d mismatches)\n",
                path, stats[MODEL_PITCH].snr_mismatch, rows);
        failed = 1;
    }
    if (INT64_C(100) * stats[MODEL_PITCH].final_matches < INT64_C(98) * (n / C2FX_HOP)) {
        fprintf(stderr, "%s: final voicing gate failed (%d/%d identical)\n",
                path, stats[MODEL_PITCH].final_matches, n / C2FX_HOP);
        failed = 1;
    }
    if (tilt_mismatches != 0) {
        fprintf(stderr, "%s: tilt decision gate failed (%d/%d mismatches)\n",
                path, tilt_mismatches, rows);
        failed = 1;
    }
    free(packed);
    free(pcm);
    return failed;
}

static int run_group(const char *pattern, size_t expected, int voice)
{
    glob_t paths = {0};
    int failed = 0;

    if (glob(pattern, 0, NULL, &paths) != 0 || paths.gl_pathc != expected) {
        fprintf(stderr, "expected %zu PCM inputs matching %s\n", expected, pattern);
        globfree(&paths);
        return 1;
    }
    for (size_t i = 0; i < paths.gl_pathc; i++) {
        failed |= run_file(paths.gl_pathv[i], voice);
    }
    globfree(&paths);
    return failed;
}

static int check_edge(const int16_t *x, const char *name, int silent, int constant)
{
    struct c2fx_enc_scratch scratch;
    c2fx_enc_row row;
    const int16_t *window = x + C2FX_PITCH_BEFORE - C2FX_ANALYSIS_CENTER;
    int32_t hz = c2fx_pitch_hz(x, 0, &scratch);
    c2fx_voicing_energy before = c2fx_voicing_snr(hz, &scratch);
    c2fx_voicing_tilt_band tilt = c2fx_voicing_tilt(window, &scratch);
    int base = c2fx_voicing_base(c2fx_voicing_snr_pass(before), tilt);
    int final = c2fx_voicing_final(1, base, 1, tilt);

    c2fx_lpc_analyze(window, &row, &scratch);
    c2fx_voicing_energy after = c2fx_voicing_snr(hz, &scratch);
    if (before.signal != after.signal || before.error != after.error ||
        tilt != c2fx_voicing_tilt(window, &scratch) || before.error > before.signal ||
        tilt < C2FX_TILT_REJECT || tilt > C2FX_TILT_ACCEPT || (final != 0 && final != 1) ||
        (constant && tilt != C2FX_TILT_REJECT) || (silent && (before.signal != 0 || final != 0))) {
        fprintf(stderr, "%s: invalid or order-dependent voicing result\n", name);
        return 1;
    }
    return 0;
}

static int edge_rows(void)
{
    int16_t x[C2FX_PITCH_SPAN] = {0};
    const int rates[] = {50, 400, 3999};
    uint32_t rng = UINT32_C(0xC2F32001);
    int failed = check_edge(x, "silence", 1, 1);

    for (size_t i = 0; i < sizeof rates / sizeof rates[0]; i++) {
        for (int j = 0; j < C2FX_PITCH_SPAN; j++) {
            x[j] = (j * 2 * rates[i] / 8000) % 2 == 0 ? INT16_MIN : INT16_MAX;
        }
        failed |= check_edge(x, "full-scale square wave", 0, 0);
    }
    for (int j = 0; j < C2FX_PITCH_SPAN; j++) {
        int32_t value;

        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        value = (int32_t)(rng >> 16);
        x[j] = (int16_t)(value > INT16_MAX ? value - 65536 : value);
    }
    failed |= check_edge(x, "full-scale noise", 0, 0);
    for (int j = 0; j < C2FX_PITCH_SPAN; j++) {
        x[j] = j % 160 == 0 ? INT16_MAX : 0;
    }
    failed |= check_edge(x, "50 Hz impulse train", 0, 0);
    for (int j = 0; j < C2FX_PITCH_SPAN; j++) {
        x[j] = INT16_MIN;
    }
    return failed | check_edge(x, "full-scale DC", 0, 1);
}

static int rule_boundaries(void)
{
    struct c2fx_enc_scratch scratch = {0};
    int16_t x[C2FX_ANALYSIS_LEN];
    int failed = 0;

    if (c2fx_voicing_snr_pass((c2fx_voicing_energy){39810, 10000}) ||
        !c2fx_voicing_snr_pass((c2fx_voicing_energy){39811, 10000}) ||
        !c2fx_voicing_snr_pass((c2fx_voicing_energy){100, 0}) ||
        c2fx_voicing_snr_pass((c2fx_voicing_energy){0, 0})) {
        fprintf(stderr, "voicing: 6 dB SNR threshold or sentinel failed\n");
        failed = 1;
    }
    c2fx_voicing_energy invalid_pitch = c2fx_voicing_snr(1000 * HZ_ONE, &scratch);
    if (invalid_pitch.signal != 0 || invalid_pitch.error != 0) {
        fprintf(stderr, "voicing: out-of-range pitch must return zero energy\n");
        failed = 1;
    }
    /* At 400 Hz, m=1..3 spans [200,1400) Hz, bins 13 through 89. */
    for (int k = 12; k <= 90; k++) {
        scratch.work.refinement.spectrum[2 * k] = 4096 * 1000;
        c2fx_voicing_energy e = c2fx_voicing_snr(400 * HZ_ONE, &scratch);
        uint64_t expected = k >= 13 && k <= 89 ? 1000000 : 0;

        if (e.signal != expected) {
            fprintf(stderr, "voicing: 400 Hz band limit failed at bin %d\n", k);
            failed = 1;
            break;
        }
        scratch.work.refinement.spectrum[2 * k] = 0;
    }
    for (int hz = 1180; hz <= 1182; hz += 2) {
        for (int i = 0; i < C2FX_ANALYSIS_LEN; i++) {
            x[i] = (int16_t)lround(24000 * cos(2 * C2M_PI * hz * (i - C2FX_ANALYSIS_CENTER) / C2M_FS_HZ));
        }
        c2fx_voicing_tilt_band tilt = c2fx_voicing_tilt(x, &scratch);
        int accept = c2m_voicing_tilt(x, C2FX_ANALYSIS_LEN, C2FX_ANALYSIS_CENTER) > C2M_VOICING_TILT_ACCEPT;
        if (accept != (hz == 1180) || c2fx_voicing_base(0, tilt) != accept) {
            fprintf(stderr, "voicing: tilt accept threshold failed at %d Hz\n", hz);
            failed = 1;
        }
    }
    for (int flags = 0; flags < 8; flags++) {
        int prev = (flags >> 2) & 1, base = (flags >> 1) & 1, next = flags & 1;
        const double model_tilts[] = {0.0, 0.5, 0.6, 1.0};

        for (size_t i = 0; i < sizeof model_tilts / sizeof model_tilts[0]; i++) {
            int expected = c2m_voicing_final(prev, base, next, model_tilts[i]);

            if (c2fx_voicing_final(prev, base, next, model_tilt_band(model_tilts[i])) != expected) {
                fprintf(stderr, "voicing: final bridge failed with flags %d and tilt %.1f\n", flags, model_tilts[i]);
                return 1;
            }
        }
    }
    return failed;
}

static int check_window_dtft(void)
{
    for (int i = 0; i < 104; i++) {
        double sum = 0.0;

        for (int n = -C2FX_ANALYSIS_CENTER; n <= C2FX_ANALYSIS_CENTER; n++) {
            double hann = (1.0 + cos(C2M_PI * n / C2FX_ANALYSIS_CENTER)) / 2.0;
            sum += hann * cos(2.0 * C2M_PI * n * i / (C2FX_DFT * 8));
        }
        long expected = lround(16384.0 * sum / C2FX_ANALYSIS_CENTER);
        if (c2fx_voicing_window_dtft[i] != expected) {
            fprintf(stderr, "voicing: Hann DTFT entry %d is %d, expected %ld\n",
                    i, c2fx_voicing_window_dtft[i], expected);
            return 1;
        }
    }
    return 0;
}

int main(void)
{
    int failed = rule_boundaries();

    failed |= check_window_dtft();
    failed |= edge_rows();
    failed |= run_group("tests/data/voice/*.pcm", 4, 1);
    failed |= run_group("tests/data/signals/*.pcm", 10, 0);
    return failed;
}

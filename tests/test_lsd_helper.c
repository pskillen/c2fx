// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The c2fx Authors

#define _POSIX_C_SOURCE 200809L

#include "test_lsd_helper.h"

#include "c2m_synth.h"

#include <math.h>
#include <stdlib.h>

#define LSD_BINS 127

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

double c2m_test_mean_lsd(
    const int16_t *left,
    const int16_t *right,
    const int16_t *gate,
    long count,
    int *used_out
)
{
    double peak = 0.0;
    double lsd_sum = 0.0;
    int used = 0;

    for (long i = 0; i < count; i++) {
        double g = fabs((double)gate[i]);

        if (g > peak) {
            peak = g;
        }
    }
    if (count == 0 || peak == 0.0) {
        *used_out = 0;
        return -1.0;
    }
    {
        double thresh = peak * peak * pow(10.0, -40.0 / 10.0);

        for (long off = 0; off + C2M_FRAME_SAMPLES <= count; off += C2M_FRAME_SAMPLES) {
            double energy = 0.0;
            double mag_l[LSD_BINS + 1];
            double mag_r[LSD_BINS + 1];
            double acc = 0.0;

            for (int n = 0; n < C2M_FRAME_SAMPLES; n++) {
                energy += (double)gate[off + n] * (double)gate[off + n];
            }
            if (energy / (double)C2M_FRAME_SAMPLES < thresh) {
                continue;
            }
            frame_mag(left + off, mag_l);
            frame_mag(right + off, mag_r);
            for (int k = 1; k <= LSD_BINS; k++) {
                double lv = mag_l[k] < 1.0 ? 1.0 : mag_l[k];
                double rv = mag_r[k] < 1.0 ? 1.0 : mag_r[k];
                double diff = 20.0 * log10(lv) - 20.0 * log10(rv);

                acc += diff * diff;
            }
            lsd_sum += sqrt(acc / (double)LSD_BINS);
            used += 1;
        }
    }
    *used_out = used;
    if (used == 0) {
        return -1.0;
    }
    return lsd_sum / (double)used;
}

int c2m_test_decode_c2(const uint8_t *bits, size_t nbytes, int16_t **pcm_out, long *count_out)
{
    long frames = (long)(nbytes / C2M_FRAME_BYTES);
    long cap;
    int16_t *pcm;
    c2m_decode state;
    long count = 0;

    if (bits == NULL || pcm_out == NULL || count_out == NULL || (nbytes % C2M_FRAME_BYTES) != 0) {
        return 1;
    }
    cap = frames * (long)C2M_FRAME_SAMPLES;
    pcm = calloc((size_t)cap, sizeof pcm[0]);
    if (pcm == NULL) {
        return 1;
    }
    c2m_decode_init(&state);
    for (long k = 0; k < frames; k++) {
        c2m_indices idx;
        int16_t decoded[C2M_FRAME_SAMPLES];

        c2m_frame_unpack(bits + (size_t)k * C2M_FRAME_BYTES, &idx);
        c2m_decode_pcm(&state, &idx, decoded);
        for (int n = 0; n < C2M_FRAME_SAMPLES; n++) {
            pcm[count] = decoded[n];
            count += 1;
        }
    }
    *pcm_out = pcm;
    *count_out = count;
    return 0;
}

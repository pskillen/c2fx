// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The c2fx Authors

#include "c2m_phase.h"

#include <math.h>

static void fft_radix2(double *re, double *im, int n, int sign)
{
    for (int i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;

        for (; (j & bit) != 0; bit >>= 1) {
            j ^= bit;
        }
        j ^= bit;
        if (i < j) {
            double tr = re[i];
            double ti = im[i];

            re[i] = re[j];
            im[i] = im[j];
            re[j] = tr;
            im[j] = ti;
        }
    }
    for (int len = 2; len <= n; len <<= 1) {
        double ang = (double)sign * 2.0 * C2M_PI / (double)len;
        double wlen_re = cos(ang);
        double wlen_im = sin(ang);

        for (int i = 0; i < n; i += len) {
            double wr = 1.0;
            double wi = 0.0;

            for (int k = 0; k < len / 2; k++) {
                int u = i + k;
                int v = u + len / 2;
                double tr = wr * re[v] - wi * im[v];
                double ti = wr * im[v] + wi * re[v];
                double nwr = wr * wlen_re - wi * wlen_im;

                re[v] = re[u] - tr;
                im[v] = im[u] - ti;
                re[u] += tr;
                im[u] += ti;
                wi = wr * wlen_im + wi * wlen_re;
                wr = nwr;
            }
        }
    }
}

void c2m_min_phase(const double power[C2M_SPECTRUM_BINS], double phase_out[C2M_SPECTRUM_BINS + 1])
{
    double re[C2M_DFT_SIZE];
    double im[C2M_DFT_SIZE];

    for (int k = 0; k < C2M_SPECTRUM_BINS; k++) {
        double p = power[k];

        if (p < 1e-30) {
            p = 1e-30;
        }
        re[k] = 0.5 * log(p);
        im[k] = 0.0;
    }
    /* Bin 256 is Nyquist. The envelope chain stops at bin 255. */
    re[C2M_SPECTRUM_BINS] = re[C2M_SPECTRUM_BINS - 1];
    im[C2M_SPECTRUM_BINS] = 0.0;
    for (int k = 1; k < C2M_SPECTRUM_BINS; k++) {
        re[C2M_DFT_SIZE - k] = re[k];
        im[C2M_DFT_SIZE - k] = 0.0;
    }

    fft_radix2(re, im, C2M_DFT_SIZE, +1);
    for (int n = 0; n < C2M_DFT_SIZE; n++) {
        re[n] /= (double)C2M_DFT_SIZE;
        im[n] = 0.0;
    }
    for (int n = 1; n < C2M_SPECTRUM_BINS; n++) {
        re[n] *= 2.0;
    }
    for (int n = C2M_SPECTRUM_BINS + 1; n < C2M_DFT_SIZE; n++) {
        re[n] = 0.0;
    }

    fft_radix2(re, im, C2M_DFT_SIZE, -1);
    for (int k = 0; k <= C2M_SPECTRUM_BINS; k++) {
        phase_out[k] = im[k];
    }
}

double c2m_advance_excitation(double phi1, double w0, int voiced)
{
    if (!voiced) {
        return phi1;
    }
    return phi1 + (double)C2M_HOP * w0;
}

double c2m_random_phase(uint32_t *state)
{
    uint32_t x = *state;
    double u;

    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    u = ((double)x + 0.5) / 4294967296.0;
    return (u * 2.0 - 1.0) * C2M_PI;
}

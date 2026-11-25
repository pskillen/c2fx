// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The c2fx Authors

#include "c2m_voicing.h"

#include "c2m_analysis.h"
#include "c2m_fft.h"
#include "c2m_quant.h"

#include <math.h>

static double sample_at(const int16_t *pcm, int n, int index)
{
    if (index < 0 || index >= n) {
        return 0.0;
    }
    return (double)pcm[index];
}

static void analysis_weights(double w[C2M_ANALYSIS_LEN])
{
    double sum = 0.0;
    double scale;

    for (int i = 0; i < C2M_ANALYSIS_LEN; i++) {
        w[i] = 0.5 - 0.5 * cos(2.0 * C2M_PI * (double)i / 278.0);
        sum += w[i] * w[i];
    }
    scale = sqrt((1.0 / 512.0) / sum);
    for (int i = 0; i < C2M_ANALYSIS_LEN; i++) {
        w[i] *= scale;
    }
}

/* DTFT of the analysis window at radian frequency dw. */
static void window_dtft(const double w[C2M_ANALYSIS_LEN], double dw, double *re, double *im)
{
    double wr = 0.0;
    double wi = 0.0;
    double c = cos(dw);
    double s = sin(dw);
    double cr = 1.0;
    double ci = 0.0;

    for (int n = 0; n < C2M_ANALYSIS_LEN; n++) {
        wr += w[n] * cr;
        wi -= w[n] * ci;
        {
            double nr = cr * c - ci * s;
            double ni = cr * s + ci * c;

            cr = nr;
            ci = ni;
        }
    }
    *re = wr;
    *im = wi;
}

int c2m_voicing_decision(const int16_t *pcm, int n, int centre, double f0_hz)
{
    double w[C2M_ANALYSIS_LEN];
    double re[C2M_VOICING_DFT];
    double im[C2M_VOICING_DFT];
    double signal = 0.0;
    double error = 0.0;
    int m_max;

    if (!(f0_hz > 0.0)) {
        return 0;
    }
    analysis_weights(w);
    for (int k = 0; k < C2M_VOICING_DFT; k++) {
        re[k] = 0.0;
        im[k] = 0.0;
    }
    for (int i = 0; i < C2M_ANALYSIS_LEN; i++) {
        re[i] = sample_at(pcm, n, centre - C2M_ANALYSIS_CENTRE + i) * w[i];
    }
    c2m_fft(re, im, C2M_VOICING_DFT);

    m_max = (int)floor(C2M_VOICING_BAND_HZ / f0_hz + 0.5);
    if (m_max < 1) {
        m_max = 1;
    }
    for (int m = 1; m <= m_max; m++) {
        double lo_hz = ((double)m - 0.5) * f0_hz;
        double hi_hz = ((double)m + 0.5) * f0_hz;
        double omega = 2.0 * C2M_PI * (double)m * f0_hz / C2M_FS_HZ;
        int k_lo = (int)ceil(lo_hz * (double)C2M_VOICING_DFT / C2M_FS_HZ - 1e-9);
        int k_hi = (int)ceil(hi_hz * (double)C2M_VOICING_DFT / C2M_FS_HZ - 1e-9);
        double num_re = 0.0;
        double num_im = 0.0;
        double den = 0.0;
        double band = 0.0;

        if (k_lo < 0) {
            k_lo = 0;
        }
        if (k_hi > C2M_VOICING_DFT / 2) {
            k_hi = C2M_VOICING_DFT / 2;
        }
        for (int k = k_lo; k < k_hi; k++) {
            double dw = 2.0 * C2M_PI * (double)k / (double)C2M_VOICING_DFT - omega;
            double wr;
            double wi;
            double pr = re[k] * re[k] + im[k] * im[k];

            window_dtft(w, dw, &wr, &wi);
            num_re += re[k] * wr + im[k] * wi;
            num_im += im[k] * wr - re[k] * wi;
            den += wr * wr + wi * wi;
            band += pr;
        }
        signal += band;
        if (den > 0.0) {
            double explained = (num_re * num_re + num_im * num_im) / den;
            double resid = band - explained;

            if (resid > 0.0) {
                error += resid;
            }
        } else {
            error += band;
        }
    }
    if (!(signal > 0.0)) {
        return 0;
    }
    if (!(error > 0.0)) {
        return 1;
    }
    return 10.0 * log10(signal / error) > C2M_VOICING_SNR_DB;
}

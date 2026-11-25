// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The c2fx Authors

#include "c2m_voicing.h"

#include "c2m_analysis.h"
#include "c2m_fft.h"
#include "c2m_quant.h"

#include <math.h>

/* [c2fx design] Bias so ceil() of a band edge does not round up when the edge
 * is an exact DFT bin and the double product lands a hair over it. At fs 8000
 * and a 512-point DFT a bin is 15.625 Hz; the product error is about 1e-13 of
 * a bin, and 1e-9 is far above that and far below any real fraction of a bin.
 */
#define C2M_VOICING_BAND_EDGE_BIAS 1e-9

/* [c2fx design] Smallest windowed-frame autocorrelation r0 treated as signal.
 * Samples are int16 values, so any real frame has r0 far above this; it
 * only catches an all-zero or constant frame, whose mean-removed r0 is zero
 * to rounding.
 */
#define C2M_VOICING_R0_FLOOR 1e-9

static double sample_at(const int16_t *pcm, int n, int index)
{
    if (index < 0 || index >= n) {
        return 0.0;
    }
    return (double)pcm[index];
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

double c2m_voicing_snr_db(const int16_t *pcm, int n, int center, double f0_hz)
{
    double w[C2M_ANALYSIS_LEN];
    double re[C2M_VOICING_DFT];
    double im[C2M_VOICING_DFT];
    double signal = 0.0;
    double error = 0.0;
    int m_max;

    if (!(f0_hz > 0.0)) {
        return C2M_VOICING_SNR_NONE_DB;
    }
    c2m_analysis_weights(w);
    for (int k = 0; k < C2M_VOICING_DFT; k++) {
        re[k] = 0.0;
        im[k] = 0.0;
    }
    for (int i = 0; i < C2M_ANALYSIS_LEN; i++) {
        re[i] = sample_at(pcm, n, center - C2M_ANALYSIS_CENTER + i) * w[i];
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
        int k_lo = (int)ceil(lo_hz * (double)C2M_VOICING_DFT / C2M_FS_HZ - C2M_VOICING_BAND_EDGE_BIAS);
        int k_hi = (int)ceil(hi_hz * (double)C2M_VOICING_DFT / C2M_FS_HZ - C2M_VOICING_BAND_EDGE_BIAS);
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
        return C2M_VOICING_SNR_NONE_DB;
    }
    if (!(error > 0.0) || signal / error > C2M_VOICING_SNR_MAX_RATIO) {
        return C2M_VOICING_SNR_MAX_DB;
    }
    return 10.0 * log10(signal / error);
}

double c2m_voicing_tilt(const int16_t *pcm, int n, int center)
{
    double w[C2M_ANALYSIS_LEN];
    double x[C2M_ANALYSIS_LEN];
    double wsum = 0.0;
    double xsum = 0.0;
    double mean;
    double r0 = 0.0;
    double r1 = 0.0;

    c2m_analysis_weights(w);
    for (int i = 0; i < C2M_ANALYSIS_LEN; i++) {
        x[i] = sample_at(pcm, n, center - C2M_ANALYSIS_CENTER + i);
        wsum += w[i];
        xsum += w[i] * x[i];
    }
    mean = xsum / wsum;
    for (int i = 0; i < C2M_ANALYSIS_LEN; i++) {
        x[i] = (x[i] - mean) * w[i];
    }
    for (int i = 0; i < C2M_ANALYSIS_LEN; i++) {
        r0 += x[i] * x[i];
        if (i + 1 < C2M_ANALYSIS_LEN) {
            r1 += x[i] * x[i + 1];
        }
    }
    if (!(r0 > C2M_VOICING_R0_FLOOR)) {
        return 0.0;
    }
    return r1 / r0;
}

int c2m_voicing_base(double snr_db, double tilt)
{
    if (tilt > C2M_VOICING_TILT_ACCEPT) {
        return 1;
    }
    return snr_db > C2M_VOICING_SNR_DB && tilt > C2M_VOICING_TILT_REJECT;
}

int c2m_voicing_final(int prev_base, int base, int next_base, double tilt)
{
    if (base) {
        return 1;
    }
    return prev_base && next_base && tilt > C2M_VOICING_TILT_BRIDGE;
}

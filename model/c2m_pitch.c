// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The c2fx Authors

#include "c2m_pitch.h"

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

static void lowpass(double fir[C2M_PITCH_LPF_TAPS])
{
    double fc = C2M_PITCH_LPF_HZ / C2M_FS_HZ;
    double sum = 0.0;
    double mid = (C2M_PITCH_LPF_TAPS - 1) / 2.0;

    for (int i = 0; i < C2M_PITCH_LPF_TAPS; i++) {
        double x = (double)i - mid;
        double h;

        if (fabs(x) < 1e-12) {
            h = 2.0 * fc;
        } else {
            h = sin(2.0 * C2M_PI * fc * x) / (C2M_PI * x);
        }
        h *= 0.54 - 0.46 * cos(2.0 * C2M_PI * (double)i / (double)(C2M_PITCH_LPF_TAPS - 1));
        fir[i] = h;
        sum += h;
    }
    for (int i = 0; i < C2M_PITCH_LPF_TAPS; i++) {
        fir[i] /= sum;
    }
}

static int local_peak(const double *mag, int k)
{
    return k > 0 && k < C2M_PITCH_DFT / 2 - 1 && mag[k] >= mag[k - 1] && mag[k] >= mag[k + 1];
}

double c2m_pitch_hz(const int16_t *pcm, int n, int centre)
{
    double fir[C2M_PITCH_LPF_TAPS];
    double sq[C2M_PITCH_SAMPLES];
    double notched[C2M_PITCH_SAMPLES];
    double low[C2M_PITCH_SAMPLES];
    double re[C2M_PITCH_DFT];
    double im[C2M_PITCH_DFT];
    double mag[C2M_PITCH_DFT / 2];
    double xm = 0.0;
    double ym = 0.0;
    int ndec = 0;
    int bin_min;
    int bin_max;
    int kbest;
    int chosen;
    double fs_dec = C2M_FS_HZ / (double)C2M_PITCH_DECIMATE;
    double f0;

    for (int i = 0; i < C2M_PITCH_SAMPLES; i++) {
        double s = sample_at(pcm, n, centre - C2M_PITCH_SAMPLES / 2 + i);

        sq[i] = s * s;
    }
    for (int i = 0; i < C2M_PITCH_SAMPLES; i++) {
        notched[i] = sq[i] - xm + C2M_PITCH_NOTCH * ym;
        xm = sq[i];
        ym = notched[i];
    }
    lowpass(fir);
    for (int i = 0; i < C2M_PITCH_SAMPLES; i++) {
        double acc = 0.0;

        for (int k = 0; k < C2M_PITCH_LPF_TAPS; k++) {
            int j = i - k;

            if (j >= 0) {
                acc += fir[k] * notched[j];
            }
        }
        low[i] = acc;
    }
    for (int i = 0; i < C2M_PITCH_DFT; i++) {
        re[i] = 0.0;
        im[i] = 0.0;
    }
    for (int i = 0; i < C2M_PITCH_SAMPLES; i += C2M_PITCH_DECIMATE) {
        int nwin = C2M_PITCH_SAMPLES / C2M_PITCH_DECIMATE;
        double w = 0.5 - 0.5 * cos(2.0 * C2M_PI * (double)ndec / (double)(nwin - 1));

        re[ndec] = low[i] * w;
        ndec += 1;
    }
    c2m_fft(re, im, C2M_PITCH_DFT);
    for (int k = 0; k < C2M_PITCH_DFT / 2; k++) {
        mag[k] = hypot(re[k], im[k]);
    }

    bin_min = (int)floor(C2M_PITCH_FMIN_HZ * (double)C2M_PITCH_DFT / fs_dec + 0.5);
    bin_max = (int)floor(C2M_PITCH_FMAX_HZ * (double)C2M_PITCH_DFT / fs_dec + 0.5);
    if (bin_min < 1) {
        bin_min = 1;
    }
    if (bin_max > C2M_PITCH_DFT / 2 - 2) {
        bin_max = C2M_PITCH_DFT / 2 - 2;
    }
    kbest = bin_min;
    for (int k = bin_min + 1; k <= bin_max; k++) {
        if (mag[k] > mag[kbest]) {
            kbest = k;
        }
    }
    chosen = kbest;
    for (int div = 2;; div++) {
        double target = (double)kbest / (double)div;
        int lo;
        int hi;
        int ck;

        if (target < (double)bin_min) {
            break;
        }
        lo = (int)floor(target) - C2M_PITCH_SUBHARM_BINS;
        hi = (int)ceil(target) + C2M_PITCH_SUBHARM_BINS;
        if (lo < bin_min) {
            lo = bin_min;
        }
        if (hi > bin_max) {
            hi = bin_max;
        }
        ck = lo;
        for (int k = lo + 1; k <= hi; k++) {
            if (mag[k] > mag[ck]) {
                ck = k;
            }
        }
        if (local_peak(mag, ck) && mag[kbest] > 0.0 &&
            mag[ck] >= C2M_PITCH_SUBHARM_RATIO * mag[kbest] && ck < chosen) {
            chosen = ck;
        }
    }

    {
        double delta = 0.0;

        if (chosen > 0 && chosen < C2M_PITCH_DFT / 2 - 1) {
            double left = mag[chosen - 1];
            double mid = mag[chosen];
            double right = mag[chosen + 1];
            double den = left - 2.0 * mid + right;

            if (den != 0.0) {
                delta = 0.5 * (left - right) / den;
            }
            if (delta > 0.5) {
                delta = 0.5;
            } else if (delta < -0.5) {
                delta = -0.5;
            }
        }
        f0 = ((double)chosen + delta) * fs_dec / (double)C2M_PITCH_DFT;
    }
    if (f0 < C2M_PITCH_FMIN_HZ) {
        f0 = C2M_PITCH_FMIN_HZ;
    } else if (f0 > C2M_PITCH_FMAX_HZ) {
        f0 = C2M_PITCH_FMAX_HZ;
    }
    return f0;
}

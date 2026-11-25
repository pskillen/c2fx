// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The c2fx Authors

#include "c2m_pitch.h"

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

/* Normalized correlation of the 2*C2M_PITCH_CORR_HALF samples around `centre`
 * with the same samples one period of f_hz earlier.
 */
static double period_corr(const int16_t *pcm, int n, int centre, double f_hz)
{
    int lag = (int)floor(C2M_FS_HZ / f_hz + 0.5);
    double ab = 0.0;
    double aa = 0.0;
    double bb = 0.0;

    for (int i = centre - C2M_PITCH_CORR_HALF; i < centre + C2M_PITCH_CORR_HALF; i++) {
        double u = sample_at(pcm, n, i);
        double v = sample_at(pcm, n, i - lag);

        ab += u * v;
        aa += u * u;
        bb += v * v;
    }
    return ab / sqrt(aa * bb + 1e-9);
}

/* Power of the windowed frame at f_hz, linearly interpolated between bins. */
static double power_at(const double *pw, double f_hz)
{
    double x = f_hz * (double)C2M_PITCH_HS_DFT / C2M_FS_HZ;
    int k = (int)floor(x);
    double t = x - (double)k;

    if (k < 0 || k + 1 >= C2M_PITCH_HS_DFT / 2) {
        return 0.0;
    }
    return pw[k] * (1.0 - t) + pw[k + 1] * t;
}

/* Refine f_hz by the harmonic sum of the analysis spectrum's power. */
static double refine_f0(const int16_t *pcm, int n, int centre, double f_hz)
{
    double w[C2M_ANALYSIS_LEN];
    double re[C2M_PITCH_HS_DFT];
    double im[C2M_PITCH_HS_DFT];
    double pw[C2M_PITCH_HS_DFT / 2 + 1];
    double best = f_hz;
    double best_e = -1.0;

    c2m_analysis_weights(w);
    for (int i = 0; i < C2M_PITCH_HS_DFT; i++) {
        re[i] = 0.0;
        im[i] = 0.0;
    }
    for (int i = 0; i < C2M_ANALYSIS_LEN; i++) {
        re[i] = sample_at(pcm, n, centre - C2M_ANALYSIS_CENTRE + i) * w[i];
    }
    c2m_fft(re, im, C2M_PITCH_HS_DFT);
    for (int k = 0; k <= C2M_PITCH_HS_DFT / 2; k++) {
        pw[k] = re[k] * re[k] + im[k] * im[k];
    }
    for (int s = -C2M_PITCH_HS_STEPS; s <= C2M_PITCH_HS_STEPS; s++) {
        double f = f_hz * (1.0 + C2M_PITCH_HS_STEP * (double)s);
        int m_max = (int)floor(C2M_PITCH_HS_BAND_HZ / f);
        double e = 0.0;

        for (int m = 1; m <= m_max; m++) {
            e += power_at(pw, (double)m * f);
        }
        if (e > best_e) {
            best_e = e;
            best = f;
        }
    }
    return best;
}

double c2m_pitch_hz(const int16_t *pcm, int n, int centre, double prev_hz)
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
        if (local_peak(mag, ck) && mag[kbest] > 0.0 && ck < chosen) {
            double f_ck = (double)ck * fs_dec / (double)C2M_PITCH_DFT;
            double ratio = C2M_PITCH_SUBHARM_RATIO;

            if (f_ck < C2M_PITCH_LOW_HZ) {
                double t = (C2M_PITCH_LOW_HZ - f_ck) / (C2M_PITCH_LOW_HZ - C2M_PITCH_FMIN_HZ);

                ratio += (C2M_PITCH_LOW_RATIO - C2M_PITCH_SUBHARM_RATIO) * (t > 1.0 ? 1.0 : t);
            }
            if (prev_hz > 0.0) {
                double rel = f_ck / prev_hz;

                if (rel > 1.0 - C2M_PITCH_TRACK_TOL && rel < 1.0 + C2M_PITCH_TRACK_TOL) {
                    ratio = C2M_PITCH_NEAR_RATIO;
                } else if (rel < 1.0 / C2M_PITCH_TRACK_JUMP) {
                    ratio = C2M_PITCH_FAR_RATIO;
                }
            }
            if (mag[ck] >= ratio * mag[kbest] &&
                period_corr(pcm, n, centre, f_ck) >=
                    period_corr(pcm, n, centre, (double)kbest * fs_dec / (double)C2M_PITCH_DFT) +
                        C2M_PITCH_CORR_MARGIN) {
                chosen = ck;
            }
        }
    }
    for (int k = bin_min; k < kbest * 2 / 3 && k < chosen; k++) {
        double f_k = (double)k * fs_dec / (double)C2M_PITCH_DFT;

        if (local_peak(mag, k) && f_k >= C2M_PITCH_ANY_MIN_HZ && mag[k] >= C2M_PITCH_ANY_RATIO * mag[kbest]) {
            chosen = k;
            break;
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
    if (C2M_PITCH_HS_STEPS > 0) {
        f0 = refine_f0(pcm, n, centre, f0);
    }
    if (f0 < C2M_PITCH_FMIN_HZ) {
        f0 = C2M_PITCH_FMIN_HZ;
    } else if (f0 > C2M_PITCH_FMAX_HZ) {
        f0 = C2M_PITCH_FMAX_HZ;
    }
    return f0;
}

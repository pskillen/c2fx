// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The c2fx Authors

#include "c2m_lpc.h"

#include <math.h>

/* [c2fx design] Bias so floor(pi/w0) does not fall short on exact integers. */
#define C2M_HARMONIC_COUNT_BIAS 1e-9

int c2m_harmonic_count(double w0)
{
    double ratio;

    if (!(w0 > 0.0)) {
        return 0;
    }
    /* L = floor(pi / w0). The bias keeps an exact integer from falling short. */
    ratio = C2M_PI / w0;
    if (ratio > (double)C2M_MAX_HARMONICS) {
        return C2M_MAX_HARMONICS;
    }
    return (int)floor(ratio + C2M_HARMONIC_COUNT_BIAS);
}

static void mul_quadratic(double *poly, int *order, double c1)
{
    int n = *order;
    double next[C2M_LPC_ORDER + 3];

    for (int i = 0; i <= n + 2; i++) {
        next[i] = 0.0;
    }
    for (int i = 0; i <= n; i++) {
        next[i] += poly[i];
        next[i + 1] += poly[i] * c1;
        next[i + 2] += poly[i];
    }
    *order = n + 2;
    for (int i = 0; i <= *order; i++) {
        poly[i] = next[i];
    }
}

void c2m_lsp_to_lpc(const double lsp_rad[C2M_LSP_ORDER], double a[C2M_LPC_ORDER + 1])
{
    double p[C2M_LPC_ORDER + 2];
    double q[C2M_LPC_ORDER + 2];
    int p_order = 1;
    int q_order = 1;

    p[0] = 1.0;
    p[1] = 1.0;
    q[0] = 1.0;
    q[1] = -1.0;
    for (int k = 2; k <= C2M_LPC_ORDER + 1; k++) {
        p[k] = 0.0;
        q[k] = 0.0;
    }
    for (int j = 0; j < C2M_LSP_ORDER; j += 2) {
        mul_quadratic(p, &p_order, -2.0 * cos(lsp_rad[j]));
    }
    for (int j = 1; j < C2M_LSP_ORDER; j += 2) {
        mul_quadratic(q, &q_order, -2.0 * cos(lsp_rad[j]));
    }
    for (int k = 0; k <= C2M_LPC_ORDER; k++) {
        a[k] = 0.5 * (p[k] + q[k]);
    }
}

static void eval_lpc(const double a[], double gamma, double angle, double *mag2)
{
    double re = 0.0;
    double im = 0.0;
    double gk = 1.0;

    for (int k = 0; k <= C2M_LPC_ORDER; k++) {
        double phase = angle * (double)k;
        double ak = a[k] * gk;

        re += ak * cos(phase);
        im -= ak * sin(phase);
        gk *= gamma;
    }
    *mag2 = re * re + im * im;
}

void c2m_lpc_power(const double a[C2M_LPC_ORDER + 1], double power[C2M_SPECTRUM_BINS])
{
    for (int bin = 0; bin < C2M_SPECTRUM_BINS; bin++) {
        double angle = 2.0 * C2M_PI * (double)bin / (double)C2M_DFT_SIZE;
        double mag2;

        eval_lpc(a, 1.0, angle, &mag2);
        power[bin] = 1.0 / (mag2 + C2M_POWER_FLOOR);
    }
}

void c2m_postfilter_power(
    const double a[C2M_LPC_ORDER + 1],
    const double power[C2M_SPECTRUM_BINS],
    double out[C2M_SPECTRUM_BINS]
)
{
    for (int bin = 0; bin < C2M_SPECTRUM_BINS; bin++) {
        double angle = 2.0 * C2M_PI * (double)bin / (double)C2M_DFT_SIZE;
        double mag2;
        double mag2_g;
        double ratio;

        eval_lpc(a, 1.0, angle, &mag2);
        eval_lpc(a, C2M_PF_GAMMA, angle, &mag2_g);
        /* |A(z/gamma) / A(z)|^(2*beta); floor only on the |A(z)|^2 denominator. */
        ratio = pow(mag2_g / (mag2 + C2M_POWER_FLOOR), C2M_PF_BETA);
        out[bin] = power[bin] * ratio;
    }
}

void c2m_rescale_power(const double ref[], double dst[], int n)
{
    double sum_ref = C2M_SPECTRUM_EPS;
    double sum_dst = C2M_SPECTRUM_EPS;
    double scale;

    for (int k = 0; k < n; k++) {
        sum_ref += ref[k];
        sum_dst += dst[k];
    }
    scale = sum_ref / sum_dst;
    for (int k = 0; k < n; k++) {
        dst[k] *= scale;
    }
}

void c2m_spectrum_shape(
    const double a[C2M_LPC_ORDER + 1],
    double energy_db,
    double shaped[C2M_SPECTRUM_BINS]
)
{
    double raw[C2M_SPECTRUM_BINS];
    double energy;

    c2m_lpc_power(a, raw);
    c2m_postfilter_power(a, raw, shaped);
    c2m_rescale_power(raw, shaped, C2M_SPECTRUM_BINS);
    energy = c2m_energy_power(energy_db);
    for (int bin = 0; bin < C2M_SPECTRUM_BINS; bin++) {
        shaped[bin] *= energy;
    }
    for (int bin = 0; bin < C2M_BASS_BINS; bin++) {
        shaped[bin] *= C2M_BASS_BOOST;
    }
}

/* Half-open band. lo = trunc((m-0.5)*w0/r + 0.5), hi capped at 256. */
static double band_power(const double *power, int harmonic, double w0)
{
    double scale = w0 * (double)C2M_DFT_SIZE / (2.0 * C2M_PI);
    int lo = (int)floor(((double)harmonic - 0.5) * scale + 0.5);
    int hi = (int)floor(((double)harmonic + 0.5) * scale + 0.5);
    double sum = 0.0;

    if (lo < 0) {
        lo = 0;
    }
    if (hi > C2M_SPECTRUM_BINS) {
        hi = C2M_SPECTRUM_BINS;
    }
    for (int bin = lo; bin < hi; bin++) {
        sum += power[bin];
    }
    return sum;
}

void c2m_envelope_sample(
    const double a[C2M_LPC_ORDER + 1],
    double w0,
    double energy_db,
    c2m_envelope *env
)
{
    int count;
    int scale_h1;

    for (int m = 0; m < C2M_MAX_HARMONICS; m++) {
        env->amp[m] = 0.0;
    }
    count = c2m_harmonic_count(w0);
    env->count = count;
    c2m_spectrum_shape(a, energy_db, env->power);
    if (count == 0) {
        return;
    }

    scale_h1 = w0 < C2M_HARM1_W0_MAX;
    for (int m = 1; m <= count; m++) {
        double p = band_power(env->power, m, w0);
        double amp;

        if (p < 0.0) {
            p = 0.0;
        }
        amp = sqrt(p);
        if (m == 1 && scale_h1) {
            amp *= C2M_HARM1_SCALE;
        }
        env->amp[m - 1] = amp;
    }
}

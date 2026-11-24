// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The c2fx Authors

#include "c2m_lpc.h"

#include <math.h>
#include <stddef.h>

void c2m_spectrum_identity(
    const double *a,
    int order,
    const double *power,
    int bins,
    double *out,
    void *user
)
{
    (void)a;
    (void)order;
    (void)user;
    for (int k = 0; k < bins; k++) {
        out[k] = power[k];
    }
}

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
    return (int)floor(ratio + 1e-9);
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

static double band_power(const double *power, int harmonic, double spacing)
{
    int lo = (int)round(((double)harmonic - 0.5) * spacing);
    int hi = (int)round(((double)harmonic + 0.5) * spacing);
    double sum = 0.0;

    /* Bands stay inside bins 1..256. Bin 0 is in no band; 256 is Nyquist. */
    if (lo < 1) {
        lo = 1;
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
    c2m_spectrum_fn shape,
    void *shape_user,
    c2m_envelope *env
)
{
    double power[C2M_SPECTRUM_BINS];
    double shaped[C2M_SPECTRUM_BINS];
    double raw[C2M_MAX_HARMONICS];
    double filtered[C2M_MAX_HARMONICS];
    double spacing;
    double sum_raw = 0.0;
    double sum_filtered = 0.0;
    double offset_db;
    double gain;
    int scale_h1;
    int count;

    for (int m = 0; m < C2M_MAX_HARMONICS; m++) {
        env->amp[m] = 0.0;
    }
    count = c2m_harmonic_count(w0);
    env->count = count;
    if (count == 0) {
        return;
    }

    for (int bin = 0; bin < C2M_SPECTRUM_BINS; bin++) {
        double angle = 2.0 * C2M_PI * (double)bin / (double)C2M_DFT_SIZE;
        double re = 0.0;
        double im = 0.0;
        double mag2;

        for (int k = 0; k <= C2M_LPC_ORDER; k++) {
            double phase = angle * (double)k;
            re += a[k] * cos(phase);
            im -= a[k] * sin(phase);
        }
        mag2 = re * re + im * im;
        /* Numerical floor so a bin on a root does not become infinity. */
        if (mag2 < 1e-30) {
            mag2 = 1e-30;
        }
        power[bin] = 1.0 / mag2;
    }

    if (shape == NULL) {
        shape = c2m_spectrum_identity;
    }
    /* provisional: spec OQ-3 */
    shape(a, C2M_LPC_ORDER, power, C2M_SPECTRUM_BINS, shaped, shape_user);

    spacing = w0 * (double)C2M_DFT_SIZE / (2.0 * C2M_PI);
    scale_h1 = w0 < C2M_HARM1_W0_MAX;
    for (int m = 1; m <= count; m++) {
        raw[m - 1] = band_power(power, m, spacing);
        filtered[m - 1] = band_power(shaped, m, spacing);
        sum_raw += raw[m - 1];
        sum_filtered += filtered[m - 1];
    }

    /* Spec 6.3: 10*log10(sum a_m^2) = 10*log10(sum B_m) + 28.8 + 1.5625*(e - 20).
     * energy_db is 1.5625*e, including a midpoint that is not an integer index.
     * K is chosen so a_m^2 = K * B'_m meets that total. B' is the hook's spectrum.
     */
    offset_db = C2M_LEVEL_OFFSET_DB + (energy_db - C2M_ENERGY_STEP_DB * 20.0);
    if (sum_filtered > 0.0) {
        gain = sum_raw * pow(10.0, offset_db / 10.0) / sum_filtered;
    } else {
        gain = 0.0;
    }
    for (int m = 1; m <= count; m++) {
        double p;
        double amp;

        p = gain * filtered[m - 1];
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

// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The c2fx Authors

#include "c2m_analysis.h"

#include <math.h>

#if C2M_INPUT_HIGHPASS
#error "the analysis window is specified on the raw input"
#endif

/* Autocorrelation method and Levinson-Durbin recursion (Rabiner and Schafer,
 * Digital Processing of Speech Signals). LSP frequencies are the ordered roots
 * of the symmetric and antisymmetric polynomials (Kabal and Ramachandran,
 * IEEE Trans. ASSP, 1986), found by bisection of the real cosine polynomial.
 */

#define C2M_LSP_GRID 4096
#define C2M_LSP_BISECT 48

static double sample_at(const int16_t *pcm, int n, int index)
{
    if (index < 0 || index >= n) {
        return 0.0;
    }
    return (double)pcm[index];
}

/* Hann, then scaled so the sum of squared weights is 1/512 (spec 10.2.2). */
void c2m_analysis_weights(double w[C2M_ANALYSIS_LEN])
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

static void windowed_frame(const int16_t *pcm, int n, int center, double x[C2M_ANALYSIS_LEN])
{
    double w[C2M_ANALYSIS_LEN];

    c2m_analysis_weights(w);
    for (int i = 0; i < C2M_ANALYSIS_LEN; i++) {
        int sample = center - C2M_ANALYSIS_CENTER + i;

        x[i] = sample_at(pcm, n, sample) * w[i];
    }
}

static void autocorrelation(const double x[C2M_ANALYSIS_LEN], double r[C2M_LPC_ORDER + 1])
{
    for (int k = 0; k <= C2M_LPC_ORDER; k++) {
        double sum = 0.0;

        for (int i = 0; i < C2M_ANALYSIS_LEN - k; i++) {
            sum += x[i] * x[i + k];
        }
        r[k] = sum;
    }
}

/* a[0] = 1. Returns the prediction-error power G^2. */
static double levinson(const double r[C2M_LPC_ORDER + 1], double a[C2M_LPC_ORDER + 1])
{
    double e = r[0];

    for (int i = 0; i <= C2M_LPC_ORDER; i++) {
        a[i] = 0.0;
    }
    a[0] = 1.0;
    if (!(e > 0.0)) {
        return 0.0;
    }
    for (int i = 1; i <= C2M_LPC_ORDER; i++) {
        double acc = r[i];
        double gamma;
        double next[C2M_LPC_ORDER + 1];

        for (int j = 1; j < i; j++) {
            acc += a[j] * r[i - j];
        }
        if (!(e > 0.0)) {
            break;
        }
        gamma = -acc / e;
        if (gamma > C2M_LPC_RHO_MAX) {
            gamma = C2M_LPC_RHO_MAX;
        } else if (gamma < -C2M_LPC_RHO_MAX) {
            gamma = -C2M_LPC_RHO_MAX;
        }
        for (int j = 0; j <= C2M_LPC_ORDER; j++) {
            next[j] = a[j];
        }
        for (int j = 1; j < i; j++) {
            next[j] = a[j] + gamma * a[i - j];
        }
        next[i] = gamma;
        for (int j = 0; j <= C2M_LPC_ORDER; j++) {
            a[j] = next[j];
        }
        e *= 1.0 - gamma * gamma;
        if (e < 0.0) {
            e = 0.0;
            break;
        }
    }
    return e;
}

static double cosine_poly(const double poly[C2M_LPC_ORDER + 1], double omega)
{
    double sum = poly[C2M_LPC_ORDER / 2];

    for (int m = 1; m <= C2M_LPC_ORDER / 2; m++) {
        sum += 2.0 * poly[C2M_LPC_ORDER / 2 - m] * cos((double)m * omega);
    }
    return sum;
}

static int cosine_roots(const double poly[C2M_LPC_ORDER + 1], double *roots, int max_roots)
{
    double lo_w = 1e-4;
    double span = C2M_PI - 2e-4;
    double prev_w = lo_w;
    double prev = cosine_poly(poly, prev_w);
    int count = 0;

    for (int i = 1; i <= C2M_LSP_GRID; i++) {
        double w = lo_w + span * (double)i / (double)C2M_LSP_GRID;
        double g = cosine_poly(poly, w);

        if (prev == 0.0 || prev * g < 0.0) {
            double lo = prev_w;
            double hi = w;
            double glo = prev;

            for (int step = 0; step < C2M_LSP_BISECT; step++) {
                double mid = 0.5 * (lo + hi);
                double gm = cosine_poly(poly, mid);

                if (glo * gm <= 0.0) {
                    hi = mid;
                } else {
                    lo = mid;
                    glo = gm;
                }
            }
            if (count < max_roots) {
                roots[count] = 0.5 * (lo + hi);
                count += 1;
            }
        }
        prev = g;
        prev_w = w;
    }
    return count;
}

static void flat_lsps(double lsp_rad[C2M_LSP_ORDER], int divisor, int first)
{
    for (int j = 0; j < C2M_LSP_ORDER; j++) {
        lsp_rad[j] = (double)(j + first) * C2M_PI / (double)divisor;
    }
}

static void lpc_to_lsp(const double a[C2M_LPC_ORDER + 1], double lsp_rad[C2M_LSP_ORDER])
{
    double expanded[C2M_LPC_ORDER + 1];
    double gamma;
    double gpow;
    double aa[C2M_LPC_ORDER + 2];
    double p_sum[C2M_LPC_ORDER + 2];
    double q_sum[C2M_LPC_ORDER + 2];
    double p_poly[C2M_LPC_ORDER + 1];
    double q_poly[C2M_LPC_ORDER + 1];
    double roots_p[8];
    double roots_q[8];
    double roots[16];
    int n_p;
    int n_q;
    int n;

    gamma = exp(-C2M_PI * C2M_LPC_BANDWIDTH_HZ / C2M_FS_HZ);
    gpow = 1.0;
    expanded[0] = 1.0;
    for (int k = 1; k <= C2M_LPC_ORDER; k++) {
        gpow *= gamma;
        expanded[k] = a[k] * gpow;
    }

    for (int k = 0; k <= C2M_LPC_ORDER; k++) {
        aa[k] = expanded[k];
    }
    aa[C2M_LPC_ORDER + 1] = 0.0;
    for (int k = 0; k <= C2M_LPC_ORDER + 1; k++) {
        double mirror = aa[C2M_LPC_ORDER + 1 - k];

        p_sum[k] = aa[k] + mirror;
        q_sum[k] = aa[k] - mirror;
    }
    p_poly[0] = p_sum[0];
    q_poly[0] = q_sum[0];
    for (int k = 1; k <= C2M_LPC_ORDER; k++) {
        p_poly[k] = p_sum[k] - p_poly[k - 1];
        q_poly[k] = q_sum[k] + q_poly[k - 1];
    }

    n_p = cosine_roots(p_poly, roots_p, 8);
    n_q = cosine_roots(q_poly, roots_q, 8);
    n = 0;
    for (int i = 0; i < n_p; i++) {
        roots[n] = roots_p[i];
        n += 1;
    }
    for (int i = 0; i < n_q; i++) {
        roots[n] = roots_q[i];
        n += 1;
    }
    for (int i = 1; i < n; i++) {
        double key = roots[i];
        int j = i;

        while (j > 0 && roots[j - 1] > key) {
            roots[j] = roots[j - 1];
            j -= 1;
        }
        roots[j] = key;
    }
    if (n != C2M_LSP_ORDER) {
        /* [c2fx design] Ten ordered roots, or the flat predictor's roots. */
        flat_lsps(lsp_rad, C2M_LPC_ORDER + 1, 1);
        return;
    }
    for (int j = 0; j < C2M_LSP_ORDER; j++) {
        lsp_rad[j] = roots[j];
    }
}

void c2m_lpc_analyze(
    const int16_t *pcm,
    int n,
    int center,
    double *energy,
    double lsp_rad[C2M_LSP_ORDER]
)
{
    double x[C2M_ANALYSIS_LEN];
    double r[C2M_LPC_ORDER + 1];
    double a[C2M_LPC_ORDER + 1];
    double g2;

    windowed_frame(pcm, n, center, x);
    autocorrelation(x, r);
    r[0] *= 1.0 + C2M_LPC_WHITE_NOISE;
    if (!(r[0] > 0.0)) {
        *energy = 0.0;
        flat_lsps(lsp_rad, C2M_LSP_FLAT_DIV, 0);
        return;
    }
    g2 = levinson(r, a);
    if (!(g2 > 0.0)) {
        g2 = 0.0;
    }
    *energy = g2;
    lpc_to_lsp(a, lsp_rad);
}

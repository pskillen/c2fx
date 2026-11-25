// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The c2fx Authors

#ifndef C2M_LPC_H
#define C2M_LPC_H

#include "c2m_quant.h"

#define C2M_LPC_ORDER C2M_LSP_ORDER

/* Spec 6.1: FFT_ENC 512, 256 power bins 0..255. */
#define C2M_DFT_SIZE 512
#define C2M_SPECTRUM_BINS 256

/* L at 50 Hz is 80. The w0 quantizer does not go below that. */
#define C2M_MAX_HARMONICS 80

/* Spec 6.4: scale harmonic 1 when w0 < pi*150/4000 rad (strict). */
#define C2M_HARM1_W0_MAX (C2M_PI * 150.0 / 4000.0)
#define C2M_HARM1_SCALE 0.032

typedef struct c2m_envelope {
    int count; /* L = floor(pi / w0) */
    double amp[C2M_MAX_HARMONICS]; /* amp[m - 1]; 0 if harmonic m is absent */
    /* Shaped power, bins 0..255, after post filter, rescale, energy, bass. */
    double power[C2M_SPECTRUM_BINS];
} c2m_envelope;

int c2m_harmonic_count(double w0);

/* a[0] = 1, a[1..10] are the coefficients of z^-1 .. z^-10. */
void c2m_lsp_to_lpc(const double lsp_rad[C2M_LSP_ORDER], double a[C2M_LPC_ORDER + 1]);

/* P(k) = 1/(|A|^2 + C2M_POWER_FLOOR) on bins 0..255. */
void c2m_lpc_power(const double a[C2M_LPC_ORDER + 1], double power[C2M_SPECTRUM_BINS]);

/* P'(k) = P(k) * |A(z/gamma) / A(z)|^(2*beta). */
void c2m_postfilter_power(
    const double a[C2M_LPC_ORDER + 1],
    const double power[C2M_SPECTRUM_BINS],
    double out[C2M_SPECTRUM_BINS]
);

/* Scale dst so sum(dst) matches sum(ref), with C2M_SPECTRUM_EPS on each sum. */
void c2m_rescale_power(const double ref[], double dst[], int n);

/* Spec 6.2 chain on the 256 power bins. Synthesis gain is applied later. */
void c2m_spectrum_shape(
    const double a[C2M_LPC_ORDER + 1],
    double energy_db,
    double shaped[C2M_SPECTRUM_BINS]
);

/* Spec 6.1. lo/hi are the half-open DFT band of harmonic m, hi capped at 256. */
void c2m_band_edges(double w0, int harmonic, int *lo, int *hi);

void c2m_envelope_sample(
    const double a[C2M_LPC_ORDER + 1],
    double w0,
    double energy_db,
    c2m_envelope *env
);

#endif

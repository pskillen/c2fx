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

/* Spec 6.3. Accurate to about 1 dB (OQ-4); this is the stated constant. */
#define C2M_LEVEL_OFFSET_DB 28.8

/* Spec 6.4: scale harmonic 1 when w0 < pi*150/4000 rad (strict). */
#define C2M_HARM1_W0_MAX (C2M_PI * 150.0 / 4000.0)
#define C2M_HARM1_SCALE 0.032

/* power has C2M_SPECTRUM_BINS samples, bins 0..255.
 * provisional: spec OQ-3. The envelope calls this and uses its power as B'.
 * c2m_spectrum_identity leaves the spectrum unchanged.
 */
typedef void (*c2m_spectrum_fn)(
    const double *a,
    int order,
    const double *power,
    int bins,
    double *out,
    void *user
);

void c2m_spectrum_identity(
    const double *a,
    int order,
    const double *power,
    int bins,
    double *out,
    void *user
);

typedef struct c2m_envelope {
    int count; /* L = floor(pi / w0) */
    double amp[C2M_MAX_HARMONICS]; /* amp[m - 1]; 0 if harmonic m is absent */
} c2m_envelope;

int c2m_harmonic_count(double w0);

/* a[0] = 1, a[1..10] are the coefficients of z^-1 .. z^-10. */
void c2m_lsp_to_lpc(const double lsp_rad[C2M_LSP_ORDER], double a[C2M_LPC_ORDER + 1]);

/* shape NULL selects c2m_spectrum_identity. */
void c2m_envelope_sample(
    const double a[C2M_LPC_ORDER + 1],
    double w0,
    double energy_db,
    c2m_spectrum_fn shape,
    void *shape_user,
    c2m_envelope *env
);

#endif

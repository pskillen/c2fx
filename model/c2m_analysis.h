// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The c2fx Authors

#ifndef C2M_ANALYSIS_H
#define C2M_ANALYSIS_H

#include "c2m_quant.h"

#include <stdint.h>

#define C2M_LPC_ORDER C2M_LSP_ORDER

/* Spec 10.2.2: odd Hann of 279 samples, advanced by 80. */
#define C2M_ANALYSIS_LEN 279
#define C2M_ANALYSIS_CENTRE 139
#define C2M_ANALYSIS_HOP 80

/* [c2fx design] No input high-pass (spec 10.2.1). An impulse's LPC energy
 * matches the raw window, and a high-pass would move the energy and the LSPs.
 */
#define C2M_INPUT_HIGHPASS 0

/* [c2fx design] Bandwidth expansion before the LSP root search, about 15 Hz.
 * gamma = exp(-pi * Hz / fs). The energy G^2 is taken before this expansion.
 * Spec 10.2.5 leaves the root finder to the implementation.
 */
#define C2M_LPC_BANDWIDTH_HZ 15.0

/* [c2fx design] Reflection-coefficient cap so a singular frame cannot make
 * the Levinson error negative. Speech frames stay inside it.
 */
#define C2M_LPC_RHO_MAX 0.999999

/* [c2fx design] Nominal LSP set when the window energy is zero, k*pi/10 for
 * k = 0..9. A zero window has no autocorrelation. This is the set the
 * reference parameter dump uses on digital silence.
 */
#define C2M_LSP_FLAT_DIV 10

/* The 279-point analysis window: Hann scaled so the sum of squares is 1/512. */
void c2m_analysis_weights(double w[C2M_ANALYSIS_LEN]);

/* Order-10 autocorrelation LPC of the analysis window centred on `centre`.
 * Samples outside [0, n) are zero. energy is G^2 (spec 10.2.6). lsp_rad is
 * ten ordered frequencies in radians.
 */
void c2m_lpc_analyse(
    const int16_t *pcm,
    int n,
    int centre,
    double *energy,
    double lsp_rad[C2M_LSP_ORDER]
);

#endif

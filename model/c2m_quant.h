// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The c2fx Authors

#ifndef C2M_QUANT_H
#define C2M_QUANT_H

#include "c2m_frame.h"

#define C2M_FS_HZ 8000.0
#define C2M_PI 3.14159265358979323846

/* Level and post filter (spec 6.2, 6.3). One set: the exponents and the
 * synthesis gain belong together. Define C2M_POSTFILTER_PDF for the PDF
 * pair (gamma 0.5, beta 0.2, synthesis gain +5.91 dB) instead of the
 * recommended pair.
 */
#if defined(C2M_POSTFILTER_PDF)
#define C2M_PF_GAMMA 0.5
#define C2M_PF_BETA 0.2
#define C2M_SYNTH_GAIN_DB 5.91
#else
#define C2M_PF_GAMMA 0.6
#define C2M_PF_BETA 0.275
#define C2M_SYNTH_GAIN_DB 6.38
#endif
#define C2M_E_MIN_DB (-10.0)
#define C2M_E_MAX_DB 40.0
#define C2M_ENERGY_STEP_DB ((C2M_E_MAX_DB - C2M_E_MIN_DB) / 32.0)
#define C2M_BASS_BOOST 1.96
#define C2M_BASS_BINS 64
#define C2M_SPECTRUM_EPS 1e-4

/* Spec 3.4: keep the LSP running sum unchanged. */
#define C2M_LSP_KEEP_RAW_SUM 1

typedef struct c2m_params {
    double f0_hz;
    double w0; /* radians per sample */
    /* Steps above index 0. Absolute dB is C2M_E_MIN_DB + energy_db. */
    double energy_db;
    double lsp_hz[C2M_LSP_ORDER];
    double lsp_rad[C2M_LSP_ORDER];
} c2m_params;

/* Spec 3.2. f0 = 50 + 350 * index / 128. */
double c2m_f0_hz(int index);
double c2m_w0_rad(int index);

void c2m_lsp_hz_to_rad(const double hz[C2M_LSP_ORDER], double rad[C2M_LSP_ORDER]);

void c2m_lsp_accept(double lsp_hz[C2M_LSP_ORDER]);

/* idx must be in the ranges c2m_frame_unpack produces. */
void c2m_dequant(const c2m_indices *idx, c2m_params *out);

/* Linear power 10^((C2M_E_MIN_DB + rel_db) / 10). rel_db is energy_db. */
double c2m_energy_power(double rel_db);

#endif

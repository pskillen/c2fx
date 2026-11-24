// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The c2fx Authors

#ifndef C2M_QUANT_H
#define C2M_QUANT_H

#include "c2m_frame.h"

#define C2M_FS_HZ 8000.0
#define C2M_PI 3.14159265358979323846

/* 50/32, the spec 3.3 energy step. Index 0 is 0 dB on this scale. */
#define C2M_ENERGY_STEP_DB (50.0 / 32.0)

/* provisional: spec OQ-8
 * 1 keeps the LSP running sum, including sums above 4 kHz and unordered pairs.
 */
#define C2M_LSP_KEEP_RAW_SUM 1

typedef struct c2m_params {
    double f0_hz;
    double w0; /* radians per sample */
    double energy_db; /* C2M_ENERGY_STEP_DB * index */
    double lsp_hz[C2M_LSP_ORDER];
    double lsp_rad[C2M_LSP_ORDER];
} c2m_params;

/* Spec 3.2. f0 = 50 + 350 * index / 128. */
double c2m_f0_hz(int index);
double c2m_w0_rad(int index);

void c2m_lsp_hz_to_rad(const double hz[C2M_LSP_ORDER], double rad[C2M_LSP_ORDER]);

/* provisional: spec OQ-8 */
void c2m_lsp_accept(double lsp_hz[C2M_LSP_ORDER]);

/* idx must be in the ranges c2m_frame_unpack produces. */
void c2m_dequant(const c2m_indices *idx, c2m_params *out);

#endif

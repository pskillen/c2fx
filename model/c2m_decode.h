// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The c2fx Authors

#ifndef C2M_DECODE_H
#define C2M_DECODE_H

#include "c2m_phase.h"

/* Spec 3.1: bit 0 voices sub-frame a and bit 1 voices sub-frame b. */
#define C2M_VOICING_BIT_PER_SUBFRAME 1

/* Spec 4 w0 rule: midpoint only when previous b and sub-frame a are voiced. */
#define C2M_W0_COPY_UNLESS_BOTH_VOICED 1

typedef struct c2m_subframe {
    int voiced;
    double f0_hz;
    double w0;
    double energy_db;
    double lsp_hz[C2M_LSP_ORDER];
    double lsp_rad[C2M_LSP_ORDER];
} c2m_subframe;

/* Previous sub-frame b. The caller owns this struct. */
typedef struct c2m_decode {
    int has_prev;
    int prev_voiced;
    double prev_w0;
    double prev_f0_hz;
    double prev_energy_db;
    double prev_lsp_hz[C2M_LSP_ORDER];
    int prev_i;
    int prev_e;
    int prev_d[C2M_LSP_ORDER];
    int lost_run;
    /* Excitation phase of harmonic 1. Initial phase is 0 (spec 4). */
    double phi1;
    uint32_t rng;
    double ola[C2M_HOP];
    c2m_noise_floor_fn noise_floor;
    void *noise_floor_user;
} c2m_decode;

void c2m_decode_init(c2m_decode *st);

/* out[0] is sub-frame a, out[1] is sub-frame b. */
void c2m_decode_frame(c2m_decode *st, const c2m_indices *idx, c2m_subframe out[2]);

#endif

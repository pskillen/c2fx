// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The c2fx Authors

#ifndef C2M_ENCODE_H
#define C2M_ENCODE_H

#include "c2m_frame.h"

#include <stdint.h>

/* One 10 ms analysis row (spec 10.2). */
typedef struct c2m_row {
    double f0_hz;
    double w0;
    double energy;
    double lsp_rad[C2M_LSP_ORDER];
    int voiced;
} c2m_row;

/* Analyse the window centred on `centre`. Samples outside [0, n) are zero. */
void c2m_analyse_row(const int16_t *pcm, int n, int centre, c2m_row *row);

/* Encode n samples, a whole number of 160-sample frames, to 8 bytes per frame.
 * Returns the frame count, or -1 if n is not a multiple of 160.
 */
int c2m_encode(const int16_t *pcm, int n, uint8_t *out);

#endif

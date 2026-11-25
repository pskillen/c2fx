// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The c2fx Authors

#include "c2m_encode.h"

#include "c2m_analysis.h"
#include "c2m_pitch.h"
#include "c2m_quant.h"
#include "c2m_voicing.h"

#include <stddef.h>

void c2m_analyse_row(const int16_t *pcm, int n, int centre, c2m_row *row)
{
    c2m_lpc_analyse(pcm, n, centre, &row->energy, row->lsp_rad);
    row->f0_hz = c2m_pitch_hz(pcm, n, centre);
    row->w0 = 2.0 * C2M_PI * row->f0_hz / C2M_FS_HZ;
    row->voiced = c2m_voicing_decision(pcm, n, centre, row->f0_hz);
}

int c2m_encode(const int16_t *pcm, int n, uint8_t *out)
{
    int frames;

    if (n < 0 || (n % (2 * C2M_ANALYSIS_HOP)) != 0) {
        return -1;
    }
    frames = n / (2 * C2M_ANALYSIS_HOP);
    for (int k = 0; k < frames; k++) {
        c2m_row row_a;
        c2m_row row_b;
        c2m_indices idx;
        int centre_b = k * (2 * C2M_ANALYSIS_HOP);

        c2m_analyse_row(pcm, n, centre_b - C2M_ANALYSIS_HOP, &row_a);
        c2m_analyse_row(pcm, n, centre_b, &row_b);
        c2m_quant_encode(row_a.voiced, row_b.voiced, row_b.w0, row_b.energy, row_b.lsp_rad, &idx);
        if (c2m_frame_pack(&idx, out + (size_t)k * C2M_FRAME_BYTES) != 0) {
            return -1;
        }
    }
    return frames;
}

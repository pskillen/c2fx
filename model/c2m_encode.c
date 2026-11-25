// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The c2fx Authors

#include "c2m_encode.h"

#include "c2m_analysis.h"
#include "c2m_pitch.h"
#include "c2m_quant.h"
#include "c2m_voicing.h"

#include <stddef.h>


/* One analysed 10 ms row, with what the voicing rules and the pitch tracker
 * need from its neighbors.
 */
typedef struct c2m_arow {
    c2m_row row;
    double snr_db;
    double tilt;
    int base;
} c2m_arow;

static void analyse_row(const int16_t *pcm, int n, int centre, const c2m_arow *prev, c2m_arow *out)
{
    c2m_lpc_analyse(pcm, n, centre, &out->row.energy, out->row.lsp_rad);
    out->row.f0_hz = c2m_pitch_hz(pcm, n, centre, prev != NULL && prev->base ? prev->row.f0_hz : 0.0);
    out->row.w0 = 2.0 * C2M_PI * out->row.f0_hz / C2M_FS_HZ;
    out->snr_db = c2m_voicing_snr_db(pcm, n, centre, out->row.f0_hz);
    out->tilt = c2m_voicing_tilt(pcm, n, centre);
    out->base = c2m_voicing_base(out->snr_db, out->tilt);
}

int c2m_encode(const int16_t *pcm, int n, uint8_t *out)
{
    int frames;
    c2m_arow w[4];

    if (n < 0 || (n % (2 * C2M_ANALYSIS_HOP)) != 0) {
        return -1;
    }
    frames = n / (2 * C2M_ANALYSIS_HOP);
    /* w[0..3] are the rows centred at 2k-2, 2k-1, 2k and 2k+1 hops. */
    analyse_row(pcm, n, -2 * C2M_ANALYSIS_HOP, NULL, &w[0]);
    for (int j = 1; j < 4; j++) {
        analyse_row(pcm, n, (j - 2) * C2M_ANALYSIS_HOP, &w[j - 1], &w[j]);
    }
    for (int k = 0; k < frames; k++) {
        c2m_indices idx;
        int v_a = c2m_voicing_final(w[0].base, w[1].base, w[2].base, w[1].tilt);
        int v_b = c2m_voicing_final(w[1].base, w[2].base, w[3].base, w[2].tilt);

        c2m_quant_encode(v_a, v_b, w[2].row.w0, w[2].row.energy, w[2].row.lsp_rad, &idx);
        if (c2m_frame_pack(&idx, out + (size_t)k * C2M_FRAME_BYTES) != 0) {
            return -1;
        }
        w[0] = w[2];
        w[1] = w[3];
        analyse_row(pcm, n, (2 * k + 2) * C2M_ANALYSIS_HOP, &w[1], &w[2]);
        analyse_row(pcm, n, (2 * k + 3) * C2M_ANALYSIS_HOP, &w[2], &w[3]);
    }
    return frames;
}

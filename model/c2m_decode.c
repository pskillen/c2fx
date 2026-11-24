// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The c2fx Authors

#include "c2m_decode.h"

void c2m_decode_init(c2m_decode *st)
{
    st->has_prev = 0;
    st->prev_voiced = 0;
    st->prev_f0_hz = c2m_f0_hz(0);
    st->prev_w0 = c2m_w0_rad(0);
    st->prev_energy_db = 0.0;
    for (int j = 0; j < C2M_LSP_ORDER; j++) {
        st->prev_lsp_hz[j] = 0.0;
    }
}

static void copy_lsp(const double hz[C2M_LSP_ORDER], double dest[C2M_LSP_ORDER])
{
    for (int j = 0; j < C2M_LSP_ORDER; j++) {
        dest[j] = hz[j];
    }
}

void c2m_decode_frame(c2m_decode *st, const c2m_indices *idx, c2m_subframe out[2])
{
    c2m_params cur;

    c2m_dequant(idx, &cur);

    if (!st->has_prev) {
        /* provisional: spec OQ-7
         * No previous b. Energy starts at the bottom step, so sub-frame a
         * is halfway down. Voicing starts unvoiced, so w0 is not blended.
         * LSPs start at this frame, so the envelope is not pulled toward 0 Hz.
         */
        st->prev_voiced = 0;
        st->prev_energy_db = 0.0;
        st->prev_f0_hz = cur.f0_hz;
        st->prev_w0 = cur.w0;
        copy_lsp(cur.lsp_hz, st->prev_lsp_hz);
    }

#if C2M_VOICING_BIT_PER_SUBFRAME
    /* provisional: spec OQ-1 */
    out[0].voiced = idx->v_a;
    out[1].voiced = idx->v_b;
#else
#error "C2M_VOICING_BIT_PER_SUBFRAME is the only voicing rule implemented"
#endif

    out[1].f0_hz = cur.f0_hz;
    out[1].w0 = cur.w0;
    out[1].energy_db = cur.energy_db;
    copy_lsp(cur.lsp_hz, out[1].lsp_hz);
    copy_lsp(cur.lsp_rad, out[1].lsp_rad);

    out[0].energy_db = 0.5 * (st->prev_energy_db + cur.energy_db);
    for (int j = 0; j < C2M_LSP_ORDER; j++) {
        out[0].lsp_hz[j] = 0.5 * (st->prev_lsp_hz[j] + cur.lsp_hz[j]);
    }
    c2m_lsp_hz_to_rad(out[0].lsp_hz, out[0].lsp_rad);

#if C2M_W0_COPY_UNLESS_BOTH_VOICED
    /* provisional: spec OQ-2 */
    if (st->prev_voiced && idx->v_b) {
        out[0].f0_hz = 0.5 * (st->prev_f0_hz + cur.f0_hz);
        out[0].w0 = 0.5 * (st->prev_w0 + cur.w0);
    } else {
        out[0].f0_hz = cur.f0_hz;
        out[0].w0 = cur.w0;
    }
#else
#error "C2M_W0_COPY_UNLESS_BOTH_VOICED is the only unvoiced w0 rule implemented"
#endif

    st->has_prev = 1;
    st->prev_voiced = idx->v_b;
    st->prev_f0_hz = cur.f0_hz;
    st->prev_w0 = cur.w0;
    st->prev_energy_db = cur.energy_db;
    copy_lsp(cur.lsp_hz, st->prev_lsp_hz);
}

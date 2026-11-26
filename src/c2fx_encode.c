// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The c2fx Authors

#include "c2fx.h"
#include "c2fx_internal.h"

#define HISTORY (C2FX_PITCH_SPAN - C2FX_HOP)
#define WINDOW_OFFSET (C2FX_PITCH_BEFORE - C2FX_ANALYSIS_CENTER)

void c2fx_enc_init(c2fx_enc_t *e)
{
    for (int j = 0; j < HISTORY; j++) {
        e->pcm[j] = 0;
    }
    e->ready = 0;
    e->even_base = 0;
    e->pending.v_b = 0;
    e->prev_hz_q20 = 0;
}

static int32_t analyze_voicing(c2fx_enc_t *e, const int16_t *x,
                           c2fx_voicing_tilt_band *tilt, struct c2fx_enc_scratch *s)
{
    int32_t hz = c2fx_pitch_hz(x, e->prev_hz_q20, s);
    int pass = c2fx_voicing_snr_pass(c2fx_voicing_snr(hz, s));

    *tilt = c2fx_voicing_tilt(x + WINDOW_OFFSET, s);
    int base = c2fx_voicing_base(pass, *tilt);
    e->prev_hz_q20 = base ? hz : 0;
    return hz;
}

void c2fx_encode(c2fx_enc_t *e, const int16_t pcm[C2FX_FRAME_LEN],
                 uint8_t bits[C2FX_BITS_BYTES], void *scratch)
{
    c2fx_scratch_t *work = scratch;
    struct c2fx_enc_scratch *s = &work->enc;
    c2fx_voicing_tilt_band odd_tilt, even_tilt;
    c2fx_enc_row row;

    for (int j = 0; j < C2FX_FRAME_LEN; j++) {
        e->pcm[HISTORY + j] = pcm[j];
    }
    /* For input starting at sample 160k, the two rows are centered at
     * 160k-80 and 160k. The odd row completes the previous frame.
     */
    analyze_voicing(e, e->pcm, &odd_tilt, s);
    int odd_base = e->prev_hz_q20 != 0;
    if (e->ready) {
        e->pending.v_b = c2fx_voicing_final(e->prev_odd_base, e->even_base,
                                           odd_base, e->even_tilt);
        c2fx_frame_pack(&e->pending, bits);
    } else {
        /* The oracle's packed frame for a zero-filled window. Its
         * predecessor at center -160 also has base voicing zero.
         */
        static const uint8_t silence[C2FX_BITS_BYTES] = {0, 0, 9, 67, 156, 228, 33, 8};

        for (int j = 0; j < C2FX_BITS_BYTES; j++) {
            bits[j] = silence[j];
        }
        e->ready = 1;
    }
    int32_t hz = analyze_voicing(e, e->pcm + C2FX_HOP, &even_tilt, s);
    int even_base = e->prev_hz_q20 != 0;
    c2fx_lpc_analyze(e->pcm + C2FX_HOP + WINDOW_OFFSET, &row, s);

    e->pending.v_a = c2fx_voicing_final(e->even_base, odd_base, even_base, odd_tilt);
    e->pending.i = c2fx_quant_w0_index(hz);
    e->pending.e = c2fx_quant_energy_index(&row);
    c2fx_quant_lsp_indices(&row, e->pending.d);
    e->prev_odd_base = (uint8_t)odd_base;
    e->even_base = (uint8_t)even_base;
    e->even_tilt = (uint8_t)even_tilt;

    for (int j = 0; j < HISTORY; j++) {
        e->pcm[j] = e->pcm[j + C2FX_FRAME_LEN];
    }
}

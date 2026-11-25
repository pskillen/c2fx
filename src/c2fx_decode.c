// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The c2fx Authors
// Public decoder. State that survives a call lives in c2fx_dec_t: the
// previous sub-frame, excitation phase, noise generator and the Q8
// overlap tail. Buffers that do not survive (two 512-point FFTs, the
// envelope spectra and the two sub-frames) come from the caller's scratch.

#include "c2fx.h"

#include "c2fx_internal.h"

#include <stddef.h>

typedef struct c2fx_scratch {
    int32_t work[C2FX_DFT];
    int32_t spec[C2FX_DFT];
    int32_t log_den[256];
    int32_t log_num[256];
    int32_t log_p[256];
    c2fx_subframe sub[2];
} c2fx_scratch;

typedef char c2fx_dec_size_ok[(sizeof(c2fx_dec_state) <= C2FX_DEC_BYTES) ? 1 : -1];
typedef char c2fx_dec_align_ok[(C2FX_DEC_BYTES % 8 == 0) ? 1 : -1];
typedef char c2fx_scratch_size_ok[(sizeof(c2fx_scratch) <= C2FX_SCRATCH_BYTES) ? 1 : -1];
typedef char c2fx_scratch_align_ok[(C2FX_SCRATCH_BYTES % 8 == 0) ? 1 : -1];

void c2fx_dec_state_init(c2fx_dec_state *st)
{
    c2fx_front_init(&st->front);
    st->prev_idx.v_a = 0;
    st->prev_idx.v_b = 0;
    st->prev_idx.i = 0;
    st->prev_idx.e = 0;
    for (int j = 0; j < C2FX_LSP_ORDER; j++) {
        st->prev_idx.d[j] = 0;
    }
    st->phi1 = 0;
    st->rng = C2FX_PHASE_SEED;
    for (int n = 0; n < C2FX_HOP; n++) {
        st->ola[n] = 0;
    }
    st->lost_run = 0;
}

void c2fx_dec_init(c2fx_dec_t *d)
{
    c2fx_dec_state_init((c2fx_dec_state *)d);
}

static void write_silence(c2fx_dec_state *st, int16_t *pcm)
{
    for (int n = 0; n < C2FX_FRAME_LEN; n++) {
        pcm[n] = 0;
    }
    for (int n = 0; n < C2FX_HOP; n++) {
        st->ola[n] = 0;
    }
}

static void flush_tail(c2fx_dec_state *st, int32_t *work, int16_t *pcm)
{
    for (int n = 0; n < C2FX_DFT; n++) {
        work[n] = 0;
    }
    c2fx_ola_q8(st->ola, work, pcm);
    c2fx_ola_q8(st->ola, work, pcm + C2FX_HOP);
}

static void synthesize_frame(c2fx_dec_state *st, c2fx_scratch *sc, int16_t *pcm)
{
    c2fx_envelope(&sc->sub[0], sc->log_den, sc->log_num, sc->log_p);
    c2fx_envelope(&sc->sub[1], sc->log_den, sc->log_num, sc->log_p);
    c2fx_synth_hop(&sc->sub[0], sc->work, sc->spec, &st->phi1, &st->rng, st->ola, pcm);
    c2fx_synth_hop(
        &sc->sub[1], sc->work, sc->spec, &st->phi1, &st->rng, st->ola, pcm + C2FX_HOP);
}

void c2fx_decode(c2fx_dec_t *d, const uint8_t *bits, int16_t pcm[C2FX_FRAME_LEN], void *scratch)
{
    c2fx_dec_state *st = (c2fx_dec_state *)d;
    c2fx_scratch *sc = (c2fx_scratch *)scratch;
    c2fx_indices idx;
    int flush = 0;

    if (bits == NULL) {
        if (!c2fx_lost_take(st, &idx, &flush)) {
            if (flush) {
                flush_tail(st, sc->work, pcm);
            } else {
                write_silence(st, pcm);
            }
            c2fx_lost_restore(st);
            return;
        }
    } else {
        st->lost_run = 0;
        c2fx_frame_unpack(bits, &idx);
        st->prev_idx = idx;
    }

    c2fx_dequant_frame(&st->front, &idx, sc->sub);
    synthesize_frame(st, sc, pcm);
}

// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The c2fx Authors
// Public decoder. State that survives a call lives in c2fx_dec_t: the
// previous sub-frame, excitation phase, noise generator and the Q8
// overlap tail. Buffers that do not survive (two 512-point FFTs, the
// envelope spectra and the two sub-frames) come from the caller's scratch.

#include "c2fx.h"

#include "c2fx_internal.h"

#include <stddef.h>

void c2fx_dec_state_init(c2fx_dec_t *st)
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
    c2fx_dec_state_init(d);
}

static void write_silence(c2fx_dec_t *st, int16_t *pcm)
{
    for (int n = 0; n < C2FX_FRAME_LEN; n++) {
        pcm[n] = 0;
    }
    for (int n = 0; n < C2FX_HOP; n++) {
        st->ola[n] = 0;
    }
}

static void flush_tail(c2fx_dec_t *st, int32_t *work, int16_t *pcm)
{
    for (int n = 0; n < C2FX_DFT; n++) {
        work[n] = 0;
    }
    c2fx_ola_q8(st->ola, work, pcm);
    c2fx_ola_q8(st->ola, work, pcm + C2FX_HOP);
}

static void synthesize_frame(c2fx_dec_t *st, struct c2fx_dec_scratch *sc, int16_t *pcm)
{
    int32_t *log_den = sc->buffers.envelope.log_den;
    int32_t *log_num = sc->buffers.envelope.log_num;
    int32_t *log_p = sc->buffers.envelope.log_p;
    int32_t *work = sc->buffers.synth.work;
    int32_t *spec = sc->buffers.synth.spec;

    c2fx_envelope(&sc->sub[0], log_den, log_num, log_p);
    c2fx_envelope(&sc->sub[1], log_den, log_num, log_p);
    c2fx_synth_hop(&sc->sub[0], work, spec, &st->phi1, &st->rng, st->ola, pcm);
    c2fx_synth_hop(&sc->sub[1], work, spec, &st->phi1, &st->rng, st->ola, pcm + C2FX_HOP);
}

void c2fx_decode(c2fx_dec_t *d, const uint8_t *bits, int16_t pcm[C2FX_FRAME_LEN], c2fx_scratch_t *scratch)
{
    struct c2fx_dec_scratch *sc = &scratch->dec;
    c2fx_indices idx;
    int flush = 0;

    if (bits == NULL) {
        if (!c2fx_lost_take(d, &idx, &flush)) {
            if (flush) {
                flush_tail(d, sc->buffers.synth.work, pcm);
            } else {
                write_silence(d, pcm);
            }
            c2fx_lost_restore(d);
            return;
        }
    } else {
        d->lost_run = 0;
        c2fx_frame_unpack(bits, &idx);
        d->prev_idx = idx;
    }

    c2fx_dequant_frame(&d->front, &idx, sc->sub);
    synthesize_frame(d, sc, pcm);
}

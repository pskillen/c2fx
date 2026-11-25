// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The c2fx Authors
// Spec 9. A null frame repeats the previous model with the energy index
// lowered by C2FX_LOST_ENERGY_STEPS, for C2FX_LOST_MAX_FRAMES frames. The
// next lost frame flushes the overlap tail. After that the output is
// silence and the decoder returns to the start-up path, keeping the
// noise generator so the next good frame draws the same phases as the model.

#include "c2fx_internal.h"

int c2fx_lost_take(c2fx_dec_state *st, c2fx_indices *idx, int *flush)
{
    *flush = 0;
    st->lost_run = (uint8_t)(st->lost_run + 1u);
    if (!st->front.ready || st->lost_run > C2FX_LOST_MAX_FRAMES) {
        /* D4 guard: the frame after the repeat limit flushes the tail. */
        if (st->lost_run == C2FX_LOST_MAX_FRAMES + 1) {
            *flush = 1;
        }
        return 0;
    }

    *idx = st->prev_idx;
    idx->v_a = st->front.prev_voiced;
    idx->v_b = st->front.prev_voiced;
    /* D4 guard: energy index steps down per lost frame. */
    if (idx->e >= C2FX_LOST_ENERGY_STEPS) {
        idx->e -= C2FX_LOST_ENERGY_STEPS;
    } else {
        idx->e = 0;
    }
    st->prev_idx = *idx;
    return 1;
}

void c2fx_lost_restore(c2fx_dec_state *st)
{
    uint32_t rng = st->rng;

    c2fx_dec_state_init(st);
    st->rng = rng;
}

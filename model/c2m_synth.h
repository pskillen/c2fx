// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The c2fx Authors

#ifndef C2M_SYNTH_H
#define C2M_SYNTH_H

#include "c2m_decode.h"

#include <stdint.h>

/* Spec 9 starting proposal. Both are [c2fx design] and named so they can move. */
#define C2M_LOST_ENERGY_STEPS 2
#define C2M_LOST_MAX_FRAMES 4

/* Triangle window and overlap-add. block is 160 samples in window order. */
void c2m_ola_hop(double ola[C2M_HOP], const double block[C2M_FRAME_SAMPLES], double out[C2M_HOP]);

/* Synthesize one 160-sample block, before overlap-add, centered on block[80].
 * Advances st->phi1. Exposed so tests can measure harmonic phase; not part of the codec API.
 */
void c2m_synth_block(c2m_decode *st, const c2m_subframe *sub, double block[C2M_FRAME_SAMPLES]);

/* Decode one frame to 160 int16 samples. A null idx means the frame was lost. */
void c2m_decode_pcm(c2m_decode *st, const c2m_indices *idx, int16_t out[C2M_FRAME_SAMPLES]);

#endif

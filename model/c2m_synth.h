// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The c2fx Authors

#ifndef C2M_SYNTH_H
#define C2M_SYNTH_H

#include "c2m_decode.h"

#include <stdint.h>

/* Triangle window and overlap-add. block is 160 samples in window order. */
void c2m_ola_hop(double ola[C2M_HOP], const double block[C2M_FRAME_SAMPLES], double out[C2M_HOP]);

/* Decode one frame to 160 int16 samples. idx must be non-NULL. */
void c2m_decode_pcm(c2m_decode *st, const c2m_indices *idx, int16_t out[C2M_FRAME_SAMPLES]);

#endif

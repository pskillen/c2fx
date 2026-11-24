// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The c2fx Authors

#ifndef C2M_PHASE_H
#define C2M_PHASE_H

#include "c2m_lpc.h"

#include <stdint.h>

/* Spec 8: hop is 80 samples. Voiced excitation advances by this many samples. */
#define C2M_HOP 80
#define C2M_FRAME_SAMPLES (2 * C2M_HOP)

/* [c2fx design] Fixed xorshift seed so one decoder is deterministic. */
#define C2M_PHASE_SEED 0xC2F03200u

/* Minimum phase of a 256-bin power spectrum, cepstrum length C2M_DFT_SIZE.
 * phase_out[k] is the phase at bin k for k = 0..256 (Nyquist copied from bin 255).
 */
void c2m_min_phase(const double power[C2M_SPECTRUM_BINS], double phase_out[C2M_SPECTRUM_BINS + 1]);

/* Voiced excitation advances by C2M_HOP * w0. Unvoiced leaves phi1 unchanged. */
double c2m_advance_excitation(double phi1, double w0, int voiced);

/* Uniform phase on (-pi, pi) from the decoder's xorshift state. */
double c2m_random_phase(uint32_t *state);

#endif

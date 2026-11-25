// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The c2fx Authors

#ifndef C2M_VOICING_H
#define C2M_VOICING_H

#include <stdint.h>

/* Spec 10.2.4, the published test alone (OQ-12). One complex sinusoid is fit
 * in each harmonic band up to 1 kHz, least squares, as in Griffin and Lim,
 * "Multiband Excitation Vocoder", IEEE Trans. ASSP, 1988. The frame is voiced
 * when the summed band SNR exceeds 6 dB.
 */
#define C2M_VOICING_SNR_DB 6.0
#define C2M_VOICING_BAND_HZ 1000.0
#define C2M_VOICING_DFT 512

/* 1 when the analysis window centred on `centre` is voiced at f0_hz. */
int c2m_voicing_decision(const int16_t *pcm, int n, int centre, double f0_hz);

#endif

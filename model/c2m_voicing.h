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

/* [c2fx design] Post-rules (OQ-12). The spec gives only the 6 dB band-SNR
 * test; these three rules are c2fx's own, chosen from the first normalized
 * autocorrelation coefficient of the windowed frame (the "tilt", a classic
 * voiced/unvoiced feature: Atal and Rabiner, IEEE Trans. ASSP, 1976). Voiced
 * speech has most of its energy at low frequencies, so tilt is near 1.
 * Fricatives and noise have tilt near 0 or below.
 *
 *  1. Reject: a row that passes the SNR test but has tilt below
 *     C2M_VOICING_TILT_REJECT is unvoiced.
 *  2. Accept: a row with tilt above C2M_VOICING_TILT_ACCEPT is voiced even
 *     when its band SNR is low, because the SNR test misses weak voicing at
 *     onsets, offsets and in low-pitched or breathy speech.
 *  3. Hysteresis: an unvoiced row between two voiced rows becomes voiced when
 *     its tilt is above C2M_VOICING_TILT_BRIDGE.
 */
#define C2M_VOICING_TILT_REJECT 0.0
#define C2M_VOICING_TILT_ACCEPT 0.6
#define C2M_VOICING_TILT_BRIDGE 0.5

/* Summed band SNR in dB: -100 when there is no signal, capped at 100. */
double c2m_voicing_snr_db(const int16_t *pcm, int n, int center, double f0_hz);

/* First normalized autocorrelation coefficient of the mean-removed, windowed
 * analysis frame centered on `center`: -1 to 1, 0 for a silent or constant frame.
 */
double c2m_voicing_tilt(const int16_t *pcm, int n, int center);

/* Rows 1 and 2 above: the base decision for one row. */
int c2m_voicing_base(double snr_db, double tilt);

/* Row 3: the final flag of the middle row of three, from the base flags of
 * its neighbors.
 */
int c2m_voicing_final(int prev_base, int base, int next_base, double tilt);

/* The 6 dB test alone, on the base rules: 1 when the window centered on
 * `center` is voiced at f0_hz, without neighbors.
 */
int c2m_voicing_decision(const int16_t *pcm, int n, int center, double f0_hz);

#endif

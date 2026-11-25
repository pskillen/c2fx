// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The c2fx Authors

#ifndef C2M_PITCH_H
#define C2M_PITCH_H

#include <stdint.h>

/* Spec 10.2.3. The preprocessor is the published one: square, notch, 48-tap
 * low-pass at 600 Hz, decimate by 5, window, 512-point DFT, peak in 50-400 Hz.
 * The octave check keeps a submultiple of that peak when the bin is a local
 * peak and at least C2M_PITCH_SUBHARM_RATIO of the peak. That submultiple rule
 * is the one in Hermes, "Measurement of pitch by subharmonic summation",
 * JASA 1988, applied to this nonlinear spectrum. The ratio is [c2fx design]
 * (OQ-11); the search width around each submultiple is too.
 */
#define C2M_PITCH_SAMPLES 320
#define C2M_PITCH_FMIN_HZ 50.0
#define C2M_PITCH_FMAX_HZ 400.0
#define C2M_PITCH_NOTCH 0.95
#define C2M_PITCH_LPF_TAPS 48
#define C2M_PITCH_LPF_HZ 600.0
#define C2M_PITCH_DECIMATE 5
#define C2M_PITCH_DFT 512
#define C2M_PITCH_SUBHARM_RATIO 0.42
#define C2M_PITCH_SUBHARM_BINS 2

/* Fundamental in Hz of the 320-sample block centred on `centre`. */
double c2m_pitch_hz(const int16_t *pcm, int n, int centre);

#endif

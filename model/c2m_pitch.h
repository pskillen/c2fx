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
#define C2M_PITCH_SUBHARM_RATIO 0.55
#define C2M_PITCH_SUBHARM_BINS 2

/* [c2fx design] Tracking (OQ-11). `prev_hz` is the pitch of the row 10 ms
 * earlier when that row was voiced, else 0. Speech pitch moves slowly and an
 * octave jump between rows is almost always an estimator error, so the
 * previous pitch moves the submultiple threshold. A candidate within
 * C2M_PITCH_TRACK_TOL (a fraction) of it needs only C2M_PITCH_NEAR_RATIO of
 * the peak. A candidate that would cut the previous pitch by more than
 * C2M_PITCH_TRACK_JUMP needs C2M_PITCH_FAR_RATIO. The peak ratio is the
 * confidence measure.
 */
#define C2M_PITCH_TRACK_TOL 0.20
#define C2M_PITCH_TRACK_JUMP 1.6
#define C2M_PITCH_NEAR_RATIO 0.39
#define C2M_PITCH_FAR_RATIO 0.79

/* [c2fx design] A submultiple must also hold up in the time domain. Its
 * normalized correlation over one of its own periods, measured over
 * 2*C2M_PITCH_CORR_HALF samples, has to come within C2M_PITCH_CORR_MARGIN of
 * the correlation at the peak's own period. The margin is negative because a
 * true period correlates a little less than its half does, but not by much.
 *
 * Submultiples below C2M_PITCH_LOW_HZ also need more of the peak: the ratio
 * moves linearly to C2M_PITCH_LOW_RATIO at 50 Hz. The 279-sample window cannot
 * resolve the harmonics of a very low pitch, so a weak low peak is leakage as
 * often as pitch.
 */
#define C2M_PITCH_CORR_HALF 100
#define C2M_PITCH_CORR_MARGIN -0.6
#define C2M_PITCH_LOW_HZ 100.0
#define C2M_PITCH_LOW_RATIO 0.47

/* [c2fx design] A strong peak below the highest one is a candidate even when
 * it is not an integer submultiple of it: the squared signal's spectrum can
 * put its top peak on a low harmonic, for example the third of a 100 Hz voice
 * where the second is missing. The lowest local peak at or above
 * C2M_PITCH_ANY_MIN_HZ with C2M_PITCH_ANY_RATIO of the top peak, and below
 * two thirds of its frequency, wins over it.
 */
#define C2M_PITCH_ANY_RATIO 0.62
#define C2M_PITCH_ANY_MIN_HZ 60.0

/* [c2fx design] Refinement. The coarse pitch is moved to the best of
 * 2*C2M_PITCH_HS_STEPS+1 values spaced C2M_PITCH_HS_STEP (a fraction) apart,
 * the best being the one whose harmonics below C2M_PITCH_HS_BAND_HZ hold the
 * most power in the analysis window's spectrum (a 2048-point DFT, power
 * interpolated between bins). This is the harmonic sum of the published
 * description, over a range of 6 percent here.
 */
#define C2M_PITCH_HS_DFT 2048
#define C2M_PITCH_HS_STEPS 12
#define C2M_PITCH_HS_STEP 0.005
#define C2M_PITCH_HS_BAND_HZ 1500.0

/* Fundamental in Hz of the 320-sample block centred on `centre`. */
double c2m_pitch_hz(const int16_t *pcm, int n, int centre, double prev_hz);

#endif

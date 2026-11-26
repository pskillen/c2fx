// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The c2fx Authors

#include "c2fx_internal.h"

#include "c2fx_window.h"
#include "dsp/c2fx_fxp.h"

#define HZ_ONE (UINT32_C(1) << 20)
#define BIN_HZ_Q20 UINT32_C(16384000) /* 8000 / 512 Hz */
#define VOICING_BAND_HZ 1000
#define WINDOW_Q 14

/* Centered Hann DTFT / DC, in Q14, at offsets i/8 of a 512-point bin.
 * Entry i is round(2^14 / 139 * sum_{n=-139..139}
 * (1 + cos(2*pi*n/278))/2 * cos(2*pi*n*i/(512*8))).
 * The largest half-band is 12.8 bins, so interpolation reaches entry 103.
 */
const int16_t c2fx_voicing_window_dtft[104] = {
    16384, 16335, 16190, 15951, 15620, 15203, 14705, 14134,
    13497, 12802, 12060, 11279, 10470, 9642, 8806, 7970,
    7145, 6339, 5560, 4815, 4110, 3450, 2840, 2283,
    1780, 1333, 942, 606, 324, 92, -92, -232,
    -332, -397, -430, -437, -423, -391, -347, -294,
    -235, -175, -116, -60, -9, 35, 71, 100,
    121, 133, 138, 136, 128, 116, 99, 80,
    60, 39, 18, -1, -18, -33, -45, -54,
    -59, -62, -61, -58, -52, -45, -36, -26,
    -16, -6, 3, 12, 19, 25, 29, 32,
    33, 32, 31, 28, 23, 19, 13, 8,
    2, -3, -8, -12, -15, -18, -19, -20,
    -19, -18, -16, -14, -11, -7, -4, 0,
};

static int32_t window_response(uint32_t bin_hz, uint32_t harmonic_hz)
{
    uint32_t offset = bin_hz >= harmonic_hz ? bin_hz - harmonic_hz : harmonic_hz - bin_hz;
    /* One table step is 2,048,000 in Q20 Hz. Keep 10 fraction bits. */
    uint32_t step = offset / 2000;
    uint32_t i = step >> 10;
    int32_t left = c2fx_voicing_window_dtft[i];
    int32_t frac = (int32_t)(step & 1023);

    return left + (((c2fx_voicing_window_dtft[i + 1] - left) * frac + 512) >> 10);
}

static uint32_t band_edge(uint32_t twice_hz)
{
    /* ceil(edge / bin width), with exact ties at integer bins. */
    return (twice_hz + 2 * BIN_HZ_Q20 - 1) / (2 * BIN_HZ_Q20);
}

c2fx_voicing_energy c2fx_voicing_snr(int32_t hz_q20, const struct c2fx_enc_scratch *s)
{
    c2fx_voicing_energy energy = {0, 0};

    if (hz_q20 < 50 * (int32_t)HZ_ONE || hz_q20 > 400 * (int32_t)HZ_ONE) {
        return energy;
    }
    const int32_t *spectrum = s->work.refinement.spectrum;
    uint32_t hz = (uint32_t)hz_q20;
    /* Rounded harmonic count is 3..20 throughout the pitch range. */
    uint32_t count = (VOICING_BAND_HZ * HZ_ONE + hz / 2) / hz;
    uint32_t lo = band_edge(hz);

    for (uint32_t m = 1; m <= count; m++) {
        uint32_t center = m * hz;
        uint32_t hi = band_edge(2 * center + hz);
        int64_t re_dot = 0, im_dot = 0;
        uint32_t norm = 0;
        int64_t band = 0;
        int64_t explained = 0;

        /* The centered FFT and centered DTFT differ from the oracle's
         * by the same bin rotation, up to a constant phase per harmonic.
         * That phase cancels when the complex dot product is squared.
         */
        /* The highest band ends at 1400 Hz. The 89 possible bins and
         * the retained FFT's near-2^29 component bound leave room for
         * both squared energies and the Q20 threshold cross-products.
         */
        for (uint32_t k = lo; k < hi; k++) {
            int32_t w = window_response(k * BIN_HZ_Q20, center);
            int32_t re = spectrum[2 * k] >> 12;
            int32_t im = spectrum[2 * k + 1] >> 12;

            band = c2fx_mac32(c2fx_mac32(band, re, re), im, im);
            re_dot = c2fx_mac32(re_dot, re, w);
            im_dot = c2fx_mac32(im_dot, im, w);
            norm += (uint32_t)(w * w);
        }
        /* Q14 norm and Q17 reciprocal. The spectrum shift is common
         * to both energies and cancels in the ratio.
         */
        uint32_t den = (norm + (1u << (WINDOW_Q - 1))) >> WINDOW_Q;
        if (den != 0) {
            uint32_t reciprocal = ((1u << 31) + den / 2) / den;
            int32_t re = (int32_t)(re_dot >> WINDOW_Q);
            int32_t im = (int32_t)(im_dot >> WINDOW_Q);
            int64_t power = c2fx_mac32(c2fx_mac32(0, re, re), im, im);

            explained = (power * reciprocal) >> 17;
        }
        energy.signal += (uint64_t)band;
        if (band > explained) {
            energy.error += (uint64_t)(band - explained);
        }
        lo = hi;
    }
    return energy;
}

int c2fx_voicing_snr_pass(c2fx_voicing_energy energy)
{
    /* Round(10^(6/10) * 2^20). Zero signal fails (-100 dB);
     * zero residual and capped ratios (100 dB) pass.
     */
    return (energy.signal << 20) > energy.error * UINT32_C(4174456);
}

c2fx_voicing_tilt_band c2fx_voicing_tilt(const int16_t x[C2FX_ANALYSIS_LEN], struct c2fx_enc_scratch *s)
{
    int32_t *hann = s->work.refinement.interp.hann;
    int64_t weighted = 0;
    int64_t r0 = 0;
    int64_t r1 = 0;
    int32_t prev = 0;

    c2fx_hann_q38(hann);
    for (int i = 1; i < C2FX_ANALYSIS_CENTER; i++) {
        int32_t pair = (int32_t)x[i] + x[C2FX_ANALYSIS_LEN - 1 - i];

        weighted = c2fx_mac32(weighted, pair, hann[i]);
    }
    weighted = c2fx_mac32(weighted, x[C2FX_ANALYSIS_CENTER], hann[C2FX_ANALYSIS_CENTER]);
    /* The Q38 taps sum to 165379480429. Normalize to a Q8 mean with
     * round(2^(22+8+31) / sum), after narrowing the weighted sum.
     */
    int32_t mean = (int32_t)((c2fx_mac32(INT64_C(1) << 30, (int32_t)(weighted >> 22), 13942739)) >> 31);

    /* Windowed samples are Q18 after shifting the Q46 product. The
     * unit Hann power (1/512) bounds r0 and |r1| below 2^60 including
     * rounding, so the threshold cross-products fit in int64_t.
     */
    for (int i = 1; i < C2FX_ANALYSIS_LEN - 1; i++) {
        int j = i <= C2FX_ANALYSIS_CENTER ? i : C2FX_ANALYSIS_LEN - 1 - i;
        int32_t sample = (int32_t)c2fx_mulshr_round(x[i] * 256 - mean, hann[j], 28);

        r0 = c2fx_mac32(r0, sample, sample);
        r1 = c2fx_mac32(r1, prev, sample);
        prev = sample;
    }
    /* r0 is Q36, so the model's 1e-9 floor is between 68 and 69. */
    if (r0 <= 68 || r1 <= 0) {
        return C2FX_TILT_REJECT;
    }
    if (5 * r1 > 3 * r0) {
        return C2FX_TILT_ACCEPT;
    }
    return 2 * r1 > r0 ? C2FX_TILT_BRIDGE : C2FX_TILT_POSITIVE;
}

int c2fx_voicing_base(int snr_pass, c2fx_voicing_tilt_band tilt)
{
    return tilt == C2FX_TILT_ACCEPT || (snr_pass && tilt > C2FX_TILT_REJECT);
}

int c2fx_voicing_final(int prev_base, int base, int next_base, c2fx_voicing_tilt_band tilt)
{
    return base || (prev_base && next_base && tilt >= C2FX_TILT_BRIDGE);
}

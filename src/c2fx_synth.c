// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The c2fx Authors
// Spec 8. One sub-frame, then the triangle overlap-add.
//
// Q formats, one sub-frame:
// 1. Amplitude is log2 Q11 from the envelope. Synthesis gain is added in
//    that domain: C2FX_SYNTH_GAIN_LOG2_Q11, real value 2.08431 (6.38 dB).
// 2. Block exponent s, an integer. The loudest harmonic's linear amplitude
//    times 2^s lands in [8192, 16384). Its DFT bin is about 2^21..2^22.
// 3. Spectrum bin k holds X[k] = 256 * A * 2^s * exp(j theta), the
//    unnormalized half-DFT of A * 2^s * cos(2 pi k n / 512 + theta).
//    Nyquist (bin 256) holds 512 * A * 2^s * cos(theta), real.
// 4. c2fx_irfft512 is unscaled: irfft(X) / 512 = A * 2^s * cos(...).
// 5. A power-of-two shift turns that into Q8 (linear sample * 256),
//    saturated at ±65536 so the window multiply stays in int32.
// 6. Triangle weights n/80 and (80-n)/80, overlap-add, still Q8.
// 7. Round half away from zero and saturate to ±32767.

#include "c2fx_internal.h"

#include "c2fx_dsp.h"

/* log2(10^(6.38/20)) in Q11. round(1.059695062 * 2048) = 2170.
 * 2^(2170/2048) = 2.08431. 10^(6.38/20) = 2.08449. The gain is 0.00075 dB low.
 */
#define C2FX_SYNTH_GAIN_LOG2_Q11 2170

/* Q8 limit: ±65536 linear samples. Above the int16 clip, and 80 * 2^24
 * still fits in a 32-bit window product.
 */
#define C2FX_Q8_LIM (65536 << 8)

/* Extra bits on the block exponent. The base scale puts the loudest
 * harmonic in [1024, 2048), a quantum of 1 there. Harmonics that cancel
 * (a minimum-phase impulse) need a finer quantum, or the residual is
 * tens of dB worse than the per-harmonic error. Three more bits make
 * the quantum 1/8 of that, and the inverse FFT still fits in int32:
 * at most about 80 harmonics, each bin under 2^22.
 */
#define C2FX_SYNTH_EXTRA_BITS 3

static int32_t sat_add(int32_t a, int32_t b)
{
    int64_t s = (int64_t)a + (int64_t)b;

    if (s > 2147483647) {
        return 2147483647;
    }
    if (s < -2147483647 - 1) {
        return -2147483647 - 1;
    }
    return (int32_t)s;
}

static int32_t q8_clamp(int32_t x)
{
    if (x > C2FX_Q8_LIM) {
        return C2FX_Q8_LIM;
    }
    if (x < -C2FX_Q8_LIM) {
        return -C2FX_Q8_LIM;
    }
    return x;
}

/* (a * b) >> s, rounded, half away from zero. */
static int32_t mul_round(int32_t a, int32_t b, int s)
{
    int64_t p = (int64_t)a * (int64_t)b;
    int64_t bias = (int64_t)1 << (s - 1);

    if (p >= 0) {
        p += bias;
    } else {
        p -= bias;
    }
    return (int32_t)(p >> s);
}

static int32_t shr_round(int32_t x, int sh)
{
    int64_t v = x;
    int64_t bias;

    if (sh <= 0) {
        return x;
    }
    if (sh >= 31) {
        return 0;
    }
    bias = (int64_t)1 << (sh - 1);
    if (v >= 0) {
        return (int32_t)((v + bias) >> sh);
    }
    return (int32_t) - ((-v + bias) >> sh);
}

/* Round half up of m * f0 / 15.625, the same rational as c2m_harm_bin.
 * Grid f0 = (6400 + 350*key) / 128, so
 *   bin = (m * (6400 + 350*key) + 1000) / 2000.
 * A midpoint uses the sum of the two indices as key:
 *   bin = (m * (12800 + 350*key) + 2000) / 4000.
 * m is at most 80 and a midpoint key at most 254, so the largest
 * intermediate is 80 * (12800 + 350*254) + 2000 = 8138000, inside int32.
 */
static int harm_bin(int m, const c2fx_subframe *sub)
{
    if (sub->pitch_mid) {
        return (m * (12800 + 350 * sub->pitch_key) + 2000) / 4000;
    }
    return (m * (6400 + 350 * sub->pitch_key) + 1000) / 2000;
}

static int floor_div_2048(int32_t x)
{
    if (x >= 0) {
        return x >> 11;
    }
    return -((-x + 2047) >> 11);
}

static void clear_buf(int32_t *x, int n)
{
    for (int i = 0; i < n; i++) {
        x[i] = 0;
    }
}

static void place_harmonic(int32_t *spec, int bin, int32_t amp_q, uint32_t theta)
{
    int32_t c = c2fx_cos(theta);
    int32_t s = c2fx_cos(theta - 0x40000000u);

    if (bin == 256) {
        /* Real Nyquist: N * A * cos(theta). 512/32768 = 1/64. */
        spec[1] = sat_add(spec[1], mul_round(amp_q, c, 6));
        return;
    }
    /* (N/2) * A * exp(j theta). 256/32768 = 1/128. */
    spec[2 * bin] = sat_add(spec[2 * bin], mul_round(amp_q, c, 7));
    spec[2 * bin + 1] = sat_add(spec[2 * bin + 1], mul_round(amp_q, s, 7));
}

/* y_qs = A * 2^s * cos. Return A * cos in Q8, times 2^(8-s). */
static int32_t to_q8(int32_t y_qs, int s)
{
    int sh = 8 - s;

    if (sh >= 0) {
        if (sh >= 16) {
            if (y_qs > 0) {
                return C2FX_Q8_LIM;
            }
            if (y_qs < 0) {
                return -C2FX_Q8_LIM;
            }
            return 0;
        }
        if (y_qs > (C2FX_Q8_LIM >> sh)) {
            return C2FX_Q8_LIM;
        }
        if (y_qs < -(C2FX_Q8_LIM >> sh)) {
            return -C2FX_Q8_LIM;
        }
        /* The clamps bound the scaled sample to ±C2FX_Q8_LIM. */
        return y_qs * ((int32_t)1 << sh);
    }
    return q8_clamp(shr_round(y_qs, -sh));
}

static int32_t mul_div80(int n, int32_t sample)
{
    int32_t prod = n * sample;

    if (prod >= 0) {
        return (prod + 40) / 80;
    }
    return -((-prod + 40) / 80);
}

static int16_t q8_to_i16(int32_t x)
{
    int32_t y = shr_round(x, 8);

    if (y > 32767) {
        return 32767;
    }
    if (y < -32767) {
        return -32767;
    }
    return (int16_t)y;
}

void c2fx_ola_q8(int32_t ola[C2FX_HOP], const int32_t y[C2FX_DFT], int16_t out[C2FX_HOP])
{
    for (int n = 0; n < C2FX_HOP; n++) {
        /* D3 guard: rising half is the end of the IDFT, falling half the start. */
        int32_t rise = q8_clamp(y[C2FX_DFT - C2FX_HOP + n]);
        int32_t fall = q8_clamp(y[n]);
        int32_t mixed = sat_add(ola[n], mul_div80(n, rise));

        out[n] = q8_to_i16(mixed);
        ola[n] = mul_div80(C2FX_HOP - n, fall);
    }
}

static void synthesize(const c2fx_subframe *sub, int32_t *work, int32_t *spec, uint32_t *phi1, uint32_t *rng)
{
    int32_t peak = -2147483647 - 1;
    int usable = 0;
    int s;
    int sh = 0;

    for (int m = 0; m < sub->count; m++) {
        int32_t loga;

        if (sub->amp_log2_q11[m] == (int16_t)-32768) {
            continue;
        }
        loga = (int32_t)sub->amp_log2_q11[m] + C2FX_SYNTH_GAIN_LOG2_Q11;
        if (loga > peak) {
            peak = loga;
        }
        usable = 1;
    }

    clear_buf(spec, C2FX_DFT);
    if (!usable) {
        c2fx_exc_advance(phi1, sub->w0, sub->voiced);
        for (int m = 1; m <= sub->count; m++) {
            (void)c2fx_exc_harmonic(*phi1, rng, sub->voiced, m);
        }
        return;
    }

    c2fx_lpc_log_inv_power(sub->lsp_q1, work);
    c2fx_min_phase(work, spec, work, &sh);
    clear_buf(spec, C2FX_DFT);

    /* s so the peak amplitude times 2^s is in [8192, 16384). */
    s = 10 - floor_div_2048(peak) + C2FX_SYNTH_EXTRA_BITS;
    c2fx_exc_advance(phi1, sub->w0, sub->voiced);
    for (int m = 1; m <= sub->count; m++) {
        uint32_t exc = c2fx_exc_harmonic(*phi1, rng, sub->voiced, m);
        int32_t loga;
        int32_t amp_q;
        int bin;
        uint32_t theta;

        if (sub->amp_log2_q11[m - 1] == (int16_t)-32768) {
            continue;
        }
        bin = harm_bin(m, sub);
        if (bin < 1 || bin > 256) {
            continue;
        }
        loga = (int32_t)sub->amp_log2_q11[m - 1] + C2FX_SYNTH_GAIN_LOG2_Q11 + s * 2048;
        amp_q = c2fx_exp2_q11(loga);
        if (amp_q == 0) {
            continue;
        }
        /* D3 guard: excitation plus the minimum phase at this bin. */
        theta = exc + c2fx_min_phase_bin(work, bin, sh);
        place_harmonic(spec, bin, amp_q, theta);
    }

    c2fx_irfft512(spec);
    for (int n = 0; n < C2FX_DFT; n++) {
        spec[n] = to_q8(shr_round(spec[n], 9), s);
    }
}

void c2fx_synth_hop(
    const c2fx_subframe *sub,
    int32_t *work,
    int32_t *spec,
    uint32_t *phi1,
    uint32_t *rng,
    int32_t ola[C2FX_HOP],
    int16_t *pcm)
{
    if (sub->count <= 0) {
        c2fx_exc_advance(phi1, sub->w0, sub->voiced);
        clear_buf(spec, C2FX_DFT);
    } else {
        synthesize(sub, work, spec, phi1, rng);
    }
    c2fx_ola_q8(ola, spec, pcm);
}

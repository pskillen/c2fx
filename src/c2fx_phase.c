// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The c2fx Authors
// Spec 7, the float model's cepstrum. log2(power) in Q11 goes in; a uint32
// phase (2^32 = one turn) comes out at each harmonic bin.
//
// The model stores 0.5 * ln(power). That is log2(power) * ln(2) / 2, so a
// Q11 log2 value is 4096 / ln(2) times the model's real cepstrum input.
// rfft512 is the unnormalized DFT. The causal cepstrum is not divided by
// 512; the second rfft therefore carries an extra factor of 512, and the
// right shift sh that keeps the cepstrum inside 2^20. The imaginary part
// converts with (ln 2 / pi) * 2^(sh + 10).

#include "c2fx_internal.h"

#include "c2fx_dsp.h"

/* log2(1e-30) in Q11. round(log2(1e-30) * 2048) = -204099.
 * Real value 1e-30, the model's floor inside the log.
 */
#define C2FX_PHASE_LOG_FLOOR_Q11 (-204099)

/* (ln 2 / pi) in Q20. round(0.220635600153 * 1048576) = 231353.
 * Relative error about 8e-7, under 0.001 degree at pi.
 */
#define C2FX_PHASE_K20 231353

/* Peak |cepstrum| after the shift. A 512-point FFT of this stays in int32. */
#define C2FX_PHASE_PEAK (1 << 20)

static int32_t clamp_log(int32_t v)
{
    if (v < C2FX_PHASE_LOG_FLOOR_Q11) {
        return C2FX_PHASE_LOG_FLOOR_Q11;
    }
    return v;
}

static int32_t sat_mul2(int32_t x)
{
    if (x > 1073741823) {
        return 2147483647;
    }
    if (x < -1073741824) {
        return -2147483647 - 1;
    }
    return x * 2;
}

static uint32_t iabs32(int32_t x)
{
    if (x >= 0) {
        return (uint32_t)x;
    }
    if (x == -2147483647 - 1) {
        return 2147483648u;
    }
    return (uint32_t)(-x);
}

/* Arithmetic shift, truncating toward -inf. v is not INT_MIN when sh > 0
 * and the high bit would matter; the cepstrum peak is capped well below.
 */
static int32_t ashr(int32_t v, int sh)
{
    if (sh <= 0) {
        return v;
    }
    if (v >= 0) {
        return v >> sh;
    }
    return -(int32_t)(iabs32(v) >> sh);
}

static int normalize_peak(int32_t *x, int n)
{
    uint32_t peak = 0;
    int sh = 0;

    for (int i = 0; i < n; i++) {
        uint32_t a = iabs32(x[i]);

        if (a > peak) {
            peak = a;
        }
    }
    if (peak > (uint32_t)C2FX_PHASE_PEAK) {
        int bits = 32 - __builtin_clz(peak);

        sh = bits - 21;
        if ((peak >> sh) > (uint32_t)C2FX_PHASE_PEAK) {
            sh += 1;
        }
        for (int i = 0; i < n; i++) {
            x[i] = ashr(x[i], sh);
        }
    }
    return sh;
}

void c2fx_min_phase(const int32_t log2_q11[256], int32_t *fft, int32_t *side, int *sh)
{
    fft[0] = clamp_log(log2_q11[0]);
    for (int k = 1; k < 256; k++) {
        int32_t v = clamp_log(log2_q11[k]);

        fft[k] = v;
        fft[C2FX_DFT - k] = v;
    }
    /* Bin 256 is Nyquist. The envelope stops at bin 255. */
    fft[256] = clamp_log(log2_q11[255]);

    c2fx_rfft512(fft);

    /* Real cepstrum, then the causal half: double 1..255, keep 0 and 256,
     * zero the negative quefrencies. Imaginary bins are the model's zeros.
     */
    side[0] = fft[0];
    side[256] = fft[1];
    for (int k = 1; k < 256; k++) {
        side[k] = sat_mul2(fft[2 * k]);
    }
    for (int k = 257; k < C2FX_DFT; k++) {
        side[k] = 0;
    }

    *sh = normalize_peak(side, C2FX_DFT);
    c2fx_rfft512(side);
}

static uint32_t phase_u32(int32_t imag, int sh)
{
    int64_t prod = (int64_t)imag * (int64_t)C2FX_PHASE_K20;
    int right = 10 - sh;

    if (right > 0) {
        int64_t bias = (int64_t)1 << (right - 1);

        if (prod >= 0) {
            prod += bias;
        } else {
            prod -= bias;
        }
        if (prod >= 0) {
            prod >>= right;
        } else {
            prod = -(-prod >> right);
        }
    } else if (right < 0) {
        int left = -right;

        if (prod >= 0) {
            prod <<= left;
        } else {
            prod = -((-prod) << left);
        }
    }
    return (uint32_t)prod;
}

uint32_t c2fx_min_phase_bin(const int32_t *spec, int bin, int sh)
{
    int32_t imag = 0;

    if (bin > 0 && bin < 256) {
        imag = spec[2 * bin + 1];
    }
    return phase_u32(imag, sh);
}

void c2fx_exc_advance(uint32_t *phi1, uint32_t w0, int voiced)
{
    if (voiced) {
        *phi1 += 80u * w0;
    }
}

static uint32_t xorshift32(uint32_t *state)
{
    uint32_t x = *state;

    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

uint32_t c2fx_exc_harmonic(uint32_t phi1, uint32_t *rng, int voiced, int m)
{
    if (!voiced) {
        /* The model's (u * 2 - 1) * pi, with u = (x + 0.5) / 2^32.
         * In turns that is u - 0.5, which is x - 2^31 plus half an ulp.
         */
        return xorshift32(rng) + 0x80000000u;
    }
    return (uint32_t)m * phi1;
}

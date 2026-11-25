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

/* The model's bin is floor(m * w0 * 512 / (2*pi) + 0.5), and w0 is
 * 2*pi*f0/8000 rounded as a double. Exact halves fall on whichever side
 * that rounding picks, so the bin is computed in binary64, not as the
 * nearest rational. Value = sig * 2^(exp-52), sig in [2^52, 2^53).
 */
typedef struct c2fx_sf {
    uint64_t sig;
    int exp;
} c2fx_sf;

static c2fx_sf sf_from_u32(uint32_t x)
{
    c2fx_sf z;

    z.sig = 0;
    z.exp = 0;
    if (x == 0u) {
        return z;
    }
    z.exp = 31 - __builtin_clz(x);
    z.sig = (uint64_t)x << (52 - z.exp);
    return z;
}

static void sf_mul128(uint64_t a, uint64_t b, uint64_t *hi, uint64_t *lo)
{
    uint64_t a0 = a & 0xffffffffu;
    uint64_t a1 = a >> 32;
    uint64_t b0 = b & 0xffffffffu;
    uint64_t b1 = b >> 32;
    uint64_t p00 = a0 * b0;
    uint64_t p01 = a0 * b1;
    uint64_t p10 = a1 * b0;
    uint64_t p11 = a1 * b1;
    uint64_t mid = (p00 >> 32) + (p01 & 0xffffffffu) + (p10 & 0xffffffffu);

    *lo = (p00 & 0xffffffffu) | (mid << 32);
    *hi = p11 + (p01 >> 32) + (p10 >> 32) + (mid >> 32);
}

static void sf_round_up(uint64_t *q, int *exp)
{
    *q += 1ull;
    if (*q == (1ull << 53)) {
        *q >>= 1;
        *exp += 1;
    }
}

static c2fx_sf sf_mul(c2fx_sf a, c2fx_sf b)
{
    uint64_t hi;
    uint64_t lo;
    uint64_t sig;
    uint64_t round_bit;
    uint64_t sticky;
    int exp = a.exp + b.exp;
    c2fx_sf z;

    sf_mul128(a.sig, b.sig, &hi, &lo);
    /* Product of two 53-bit sigs is in [2^104, 2^106). Bit 105 of the
     * 128-bit product is bit 41 of hi (128-105 = 23? ).
     * Bit k of the product is: if k>=64, hi bit (k-64), else lo bit k.
     * Bit 105 -> hi bit 41. Bit 104 -> hi bit 40.
     */
    if ((hi >> 41) != 0ull) {
        /* >= 2^105. Keep bits 105..53. Round bit is bit 52. */
        sig = (hi << 11) | (lo >> 53);
        round_bit = (lo >> 52) & 1ull;
        sticky = (lo & ((1ull << 52) - 1ull)) != 0ull;
        exp += 1;
    } else {
        sig = (hi << 12) | (lo >> 52);
        round_bit = (lo >> 51) & 1ull;
        sticky = (lo & ((1ull << 51) - 1ull)) != 0ull;
    }
    if (round_bit && (sticky || (sig & 1ull))) {
        sf_round_up(&sig, &exp);
    }
    z.sig = sig;
    z.exp = exp;
    return z;
}

/* Quotient of a 106-bit numerator (hi:lo) by a 53-bit divisor. */
static uint64_t sf_div_bits(uint64_t hi, uint64_t lo, uint64_t d, uint64_t *rem)
{
    uint64_t q = 0;
    uint64_t r = 0;

    for (int i = 105; i >= 0; i--) {
        uint64_t bit = (i >= 64) ? ((hi >> (i - 64)) & 1ull) : ((lo >> i) & 1ull);

        r = (r << 1) | bit;
        if (r >= d) {
            r -= d;
            if (i < 64) {
                q |= 1ull << i;
            }
        }
    }
    *rem = r;
    return q;
}

static c2fx_sf sf_div(c2fx_sf a, c2fx_sf b)
{
    uint64_t hi;
    uint64_t lo;
    uint64_t q;
    uint64_t rem;
    int exp = a.exp - b.exp - 1;
    c2fx_sf z;

    /* numerator = sig << 53, a 106-bit value. */
    hi = a.sig >> 11;
    lo = a.sig << 53;
    q = sf_div_bits(hi, lo, b.sig, &rem);
    if (q >= (1ull << 53)) {
        uint64_t round_bit = q & 1ull;

        q >>= 1;
        exp += 1;
        if (round_bit && (rem != 0ull || (q & 1ull))) {
            sf_round_up(&q, &exp);
        }
    } else if (((rem << 1) > b.sig) || (((rem << 1) == b.sig) && (q & 1ull))) {
        sf_round_up(&q, &exp);
    }
    z.sig = q;
    z.exp = exp;
    return z;
}

static c2fx_sf sf_add(c2fx_sf a, c2fx_sf b)
{
    uint64_t round_bit = 0;
    uint64_t sticky = 0;
    uint64_t sb;
    uint64_t sum;
    int exp;
    int d;
    c2fx_sf z;

    if (b.sig == 0ull) {
        return a;
    }
    if (a.sig == 0ull) {
        return b;
    }
    if (a.exp < b.exp) {
        c2fx_sf t = a;

        a = b;
        b = t;
    }
    d = a.exp - b.exp;
    exp = a.exp;
    if (d == 0) {
        sb = b.sig;
    } else if (d >= 64) {
        sb = 0;
        sticky = 1;
    } else {
        round_bit = (b.sig >> (d - 1)) & 1ull;
        if (d >= 2) {
            sticky = (b.sig & ((1ull << (d - 1)) - 1ull)) != 0ull;
        }
        sb = b.sig >> d;
    }
    sum = a.sig + sb;
    if (sum >= (1ull << 53)) {
        sticky = sticky || round_bit;
        round_bit = sum & 1ull;
        sum >>= 1;
        exp += 1;
    }
    if (round_bit && (sticky || (sum & 1ull))) {
        sf_round_up(&sum, &exp);
    }
    z.sig = sum;
    z.exp = exp;
    return z;
}

static c2fx_sf sf_pi(void)
{
    c2fx_sf p;

    /* Double of the model's pi literal: exponent 1, significand as below. */
    p.sig = (1ull << 52) | 0x921fb54442d18ull;
    p.exp = 1;
    return p;
}

static c2fx_sf sf_f0(int index)
{
    c2fx_sf f = sf_from_u32(6400u + 350u * (uint32_t)index);

    /* /128, exact. */
    f.exp -= 7;
    return f;
}

static c2fx_sf sf_w0(int index)
{
    c2fx_sf two = sf_from_u32(2);
    c2fx_sf eightk = sf_from_u32(8000);

    return sf_div(sf_mul(sf_mul(two, sf_pi()), sf_f0(index)), eightk);
}

static c2fx_sf sf_w0_pair(int prev, int cur)
{
    c2fx_sf sum = sf_add(sf_w0(prev), sf_w0(cur));

    /* *1/2, exact. */
    sum.exp -= 1;
    return sum;
}

static int harm_bin(int m, const c2fx_subframe *sub)
{
    c2fx_sf w0 = sub->pitch_mid ? sf_w0_pair(sub->pitch_prev, sub->pitch_key - sub->pitch_prev)
                                : sf_w0(sub->pitch_key);
    c2fx_sf bin = sf_div(
        sf_mul(sf_mul(sf_from_u32((uint32_t)m), w0), sf_from_u32(512)),
        sf_mul(sf_from_u32(2), sf_pi()));
    uint64_t sig = bin.sig;
    int exp = bin.exp;
    uint64_t ip;
    uint64_t frac_num;
    int shift;

    /* floor(value + 0.5). value = sig * 2^(exp-52). */
    if (exp < 0) {
        return 0;
    }
    if (exp >= 52) {
        return (int)(sig << (exp - 52));
    }
    shift = 52 - exp;
    ip = sig >> shift;
    frac_num = sig & ((1ull << shift) - 1ull);
    /* frac >= 0.5 when the top bit of the fraction is set. */
    if (frac_num >= (1ull << (shift - 1))) {
        ip += 1ull;
    }
    return (int)ip;
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
        return y_qs << sh;
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

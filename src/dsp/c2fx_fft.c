// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Copyright (c) 2026 The c2fx Authors
// Unscaled 512-point real FFT. The complex core is a radix-2 decimation in
// time with Q15 twiddles from the quarter-wave table, matching RadioFoundry's
// mbe_rfft / mbe_irfft at N = 256 (512 real samples).

#include "c2fx_dsp.h"

#ifndef C2FX_EXTERNAL_DSP

#include "c2fx_fxp.h"

#define FFT_N 256

static int32_t sin_idx(uint32_t i)
{
    uint32_t q = i & 255u;
    int32_t v = c2fx_sin_tab[(i & 256u) ? 256u - q : q];

    if ((i & 512u) != 0u) {
        v = -v;
    }
    return v;
}

/* Q15 twiddle times a 32-bit sample. */
static int32_t mulq15(int32_t a, int32_t b)
{
    return c2fx_mulshr16(a, b, 15);
}

/* Complex FFT of FFT_N points. inv selects the sign of the twiddle. */
static void cfft(int32_t *z, int inv)
{
    const int n = FFT_N;

    for (int i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;

        for (; (j & bit) != 0; bit >>= 1) {
            j ^= bit;
        }
        j |= bit;
        if (i < j) {
            int32_t t = z[2 * i];

            z[2 * i] = z[2 * j];
            z[2 * j] = t;
            t = z[2 * i + 1];
            z[2 * i + 1] = z[2 * j + 1];
            z[2 * j + 1] = t;
        }
    }

    for (int len = 2, step = 512; len <= n; len <<= 1, step >>= 1) {
        int half = len >> 1;

        for (int i = 0; i < n; i += len) {
            int32_t *a = z + 2 * i;
            int32_t *b = z + 2 * (i + half);
            int32_t tr = b[0];
            int32_t ti = b[1];

            b[0] = a[0] - tr;
            b[1] = a[1] - ti;
            a[0] += tr;
            a[1] += ti;
        }
        if (half < 2) {
            continue;
        }
        for (int i = half >> 1; i < n; i += len) {
            int32_t *a = z + 2 * i;
            int32_t *b = z + 2 * (i + half);
            int32_t tr = inv ? -b[1] : b[1];
            int32_t ti = inv ? b[0] : -b[0];

            b[0] = a[0] - tr;
            b[1] = a[1] - ti;
            a[0] += tr;
            a[1] += ti;
        }
        for (int k = 1; k < half; k++) {
            int32_t c;
            int32_t s;

            if (2 * k == half) {
                continue;
            }
            c = sin_idx((uint32_t)(k * step + 256));
            s = sin_idx((uint32_t)(k * step));
            if (!inv) {
                s = -s;
            }
            for (int i = k; i < n; i += len) {
                int32_t *a = z + 2 * i;
                int32_t *b = z + 2 * (i + half);
                int32_t tr = mulq15(b[0], c) - mulq15(b[1], s);
                int32_t ti = mulq15(b[0], s) + mulq15(b[1], c);

                b[0] = a[0] - tr;
                b[1] = a[1] - ti;
                a[0] += tr;
                a[1] += ti;
            }
        }
    }
}

void c2fx_irfft512(int32_t *buf)
{
    const int n = FFT_N;
    int32_t x0 = buf[0];
    int32_t xn = buf[1];

    buf[0] = x0 + xn;
    buf[1] = x0 - xn;
    for (int k = 1; k <= n / 2; k++) {
        int32_t *p = buf + 2 * k;
        int32_t *q = buf + 2 * (n - k);
        int32_t ar = p[0];
        int32_t ai = p[1];
        int32_t br = q[0];
        int32_t bi = q[1];
        int32_t sr = ar + br;
        int32_t si = ai - bi;
        int32_t dr = ar - br;
        int32_t di = ai + bi;
        int32_t c = sin_idx((uint32_t)(k * 512 / n + 256));
        int32_t s = sin_idx((uint32_t)(k * 512 / n));
        int32_t er = mulq15(dr, c) - mulq15(di, s);
        int32_t ei = mulq15(dr, s) + mulq15(di, c);

        p[0] = sr - ei;
        p[1] = si + er;
        if (k != n - k) {
            q[0] = sr + ei;
            q[1] = -si + er;
        }
    }
    cfft(buf, 1);
}

void c2fx_rfft512(int32_t *buf)
{
    const int n = FFT_N;
    int32_t z0r;
    int32_t z0i;

    cfft(buf, 0);
    z0r = buf[0];
    z0i = buf[1];
    buf[0] = z0r + z0i;
    buf[1] = z0r - z0i;
    for (int k = 1; k <= n / 2; k++) {
        int32_t *p = buf + 2 * k;
        int32_t *q = buf + 2 * (n - k);
        int32_t ar = p[0];
        int32_t ai = p[1];
        int32_t br = q[0];
        int32_t bi = q[1];
        int32_t sr = (ar + br) >> 1;
        int32_t si = (ai - bi) >> 1;
        int32_t dr = (ar - br) >> 1;
        int32_t di = (ai + bi) >> 1;
        int32_t c = sin_idx((uint32_t)(k * 512 / n + 256));
        int32_t s = -sin_idx((uint32_t)(k * 512 / n));
        int32_t er = mulq15(dr, c) - mulq15(di, s);
        int32_t ei = mulq15(dr, s) + mulq15(di, c);

        p[0] = sr + ei;
        p[1] = si - er;
        if (k != n - k) {
            q[0] = sr - ei;
            q[1] = -si - er;
        }
    }
}

#endif

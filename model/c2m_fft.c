// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The c2fx Authors

#include "c2m_fft.h"

#include "c2m_quant.h"

#include <math.h>

void c2m_fft(double *re, double *im, int n)
{
    for (int i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;

        for (; (j & bit) != 0; bit >>= 1) {
            j ^= bit;
        }
        j ^= bit;
        if (i < j) {
            double tr = re[i];
            double ti = im[i];

            re[i] = re[j];
            im[i] = im[j];
            re[j] = tr;
            im[j] = ti;
        }
    }
    for (int len = 2; len <= n; len <<= 1) {
        double ang = -2.0 * C2M_PI / (double)len;
        double wlen_re = cos(ang);
        double wlen_im = sin(ang);

        for (int i = 0; i < n; i += len) {
            double wr = 1.0;
            double wi = 0.0;

            for (int k = 0; k < len / 2; k++) {
                int u = i + k;
                int v = u + len / 2;
                double tr = wr * re[v] - wi * im[v];
                double ti = wr * im[v] + wi * re[v];
                double nwr = wr * wlen_re - wi * wlen_im;

                re[v] = re[u] - tr;
                im[v] = im[u] - ti;
                re[u] += tr;
                im[u] += ti;
                wi = wr * wlen_im + wi * wlen_re;
                wr = nwr;
            }
        }
    }
}

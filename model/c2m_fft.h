// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The c2fx Authors

#ifndef C2M_FFT_H
#define C2M_FFT_H

/* In-place radix-2 FFT, n a power of two. Forward transform, exp(-j). */
void c2m_fft(double *re, double *im, int n);

#endif

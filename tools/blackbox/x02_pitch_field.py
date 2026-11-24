#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 The c2fx Authors
"""x02: the fundamental-frequency field: bits 2..8, Gray coded, f0 = 50 + 350*i/128 Hz.

Method: decode 60 identical frames per index and measure the frequency of the harmonics of the output
(peak of a long, zero-padded FFT, parabolic interpolation); fit f0 through the origin.
Also checks that c2enc's own indices follow the same law on synthetic voiced input.
"""
import numpy as np
import c2bb as b

G = [10] * 10


def harmonic_f0(seg, f0_guess, n=10):
    seg = np.asarray(seg, float) * np.hanning(len(seg))
    nfft = 1 << 17
    S = np.abs(np.fft.rfft(seg, nfft))
    f = np.fft.rfftfreq(nfft, 1 / b.FS)
    pk = []
    for m in range(1, n + 1):
        sel = np.where((f > m * f0_guess - f0_guess / 3) & (f < m * f0_guess + f0_guess / 3))[0]
        k = sel[np.argmax(S[sel])]
        y0, y1, y2 = np.log(S[k - 1]), np.log(S[k]), np.log(S[k + 1])
        pk.append(f[k] + 0.5 * (y0 - y2) / (y0 - 2 * y1 + y2) * (f[1] - f[0]))
    pk, m = np.array(pk), np.arange(1, n + 1)
    return float((pk * m).sum() / (m * m).sum()), float(np.abs(pk - m * (pk * m).sum() / (m * m).sum()).max())


def main():
    print("idx  measured f0   50+350*i/128   50+350*i/127   max harmonic deviation from m*f0")
    worst = 0
    for i in list(range(0, 128, 9)) + [1, 2, 3, 126, 127]:
        out = b.decode([b.make_frame(1, 1, i, 22, G)] * 60)
        f, dev = harmonic_f0(out[4000:7500], b.w0_hz(i))
        worst = max(worst, abs(f - b.w0_hz(i)))
        print(f"{i:3d}  {f:10.3f}  {b.w0_hz(i):12.3f}  {50 + i * 350 / 127:12.3f}   {dev:.4f}")
    print("worst |measured - (50+350*i/128)| =", round(worst, 4), "Hz")

    print("\nc2enc on synthetic voiced input: Gray-decoded w0 index vs input f0")
    for f0 in (60, 100, 150, 200, 300, 390):
        fr = b.encode(b.voiced_pulses(f0, 1.0))[15:30]
        idx = [b.gray_dec(b.field(x, 2, 7)) for x in fr]
        print(f"f0 {f0:3d} -> indices {sorted(set(idx))}  = {[round(b.w0_hz(i), 1) for i in sorted(set(idx))]} Hz")


if __name__ == "__main__":
    main()

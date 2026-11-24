#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 The c2fx Authors
"""x07: how the first sub-frame of a frame is obtained from the previous and the current frame.

Decode 6 frames of A then 6 of B (w0 constant, so each harmonic is a steady sinusoid) and follow the
amplitude envelope of one harmonic over time (demodulate, 40-sample boxcar). The envelope is a
piecewise-linear ramp whose knots are the sub-frame amplitudes at sub-frame centres t = 80, 160 (mod 160)
within the frame: frame k covers samples 160k..160k+159, its first sub-frame is centred on 160k+80 and
its second on 160k+160.

Part A (energy): knot at the first sub-frame of the first B frame vs the geometric mean of A and B.
Part B (LSP): the knot level of each harmonic vs the model with LSPs interpolated at weight q between A and B;
the best q over several (A, B) pairs.
"""
import numpy as np
import c2bb as b

WI = 55
F0 = b.w0_hz(WI)
L = int(4000 // F0)
FH = F0 * np.arange(1, L + 1)


def shape_amp(f_hz):
    a = b.lsp_to_lpc(f_hz)
    A1, A2 = b.aresp(a, FH), b.aresp(a, FH, 0.5)
    d = -20 * np.log10(abs(A1)) + 0.2 * 20 * np.log10(abs(A2) / abs(A1)) + 3.0 * (FH < 1000)
    p = 10 ** (d / 10)
    return np.sqrt(p / p.mean())


def main():
    print("A. energy: frames 0-5 at index ea, frames 6-11 at index eb")
    G = [10] * 10
    for ea, eb in ((10, 20), (20, 10), (8, 24)):
        out = b.decode([b.make_frame(1, 1, WI, ea, G)] * 6 + [b.make_frame(1, 1, WI, eb, G)] * 6)
        env = b.harm_env(out, 3 * F0)
        A, B, K = env[700:900].mean(), env[1300:1700].mean(), env[1040]
        print(f"  {ea}->{eb}: A {A:.1f} B {B:.1f} knot@1040 {K:.1f}  geometric mean {np.sqrt(A * B):.1f}  arithmetic mean {(A + B) / 2:.1f}")
        print("   ramp (fraction of the way A->B) at t=900..1200 step 20:", np.round((env[900:1200:20] - A) / (B - A), 2))

    print("\nB. LSP: best interpolation weight q per case (rms dB of knot level vs model)")
    cases = [([10] * 10, [10, 10, 10, 10, 18, 10, 10, 10, 10, 10]),
             ([10] * 10, [16, 10, 10, 10, 10, 10, 10, 10, 10, 10]),
             ([10] * 10, [10, 10, 10, 10, 10, 10, 10, 10, 12, 20]),
             ([12] * 9 + [8], [8] + [12] * 9)]
    for gA, gB in cases:
        fA, fB = b.lsp_hz_from_indices(gA), b.lsp_hz_from_indices(gB)
        out = b.decode([b.make_frame(1, 1, WI, 22, gA)] * 6 + [b.make_frame(1, 1, WI, 22, gB)] * 6)
        H = np.array([[b.harm_env(out, m * F0)[t] for t in (850, 1040, 1230)] for m in range(2, L + 1)])
        sA, sB = shape_amp(fA)[1:], shape_amp(fB)[1:]
        k = 0.5 * (np.median(H[:, 0] / sA) + np.median(H[:, 2] / sB))
        res = []
        for q in np.linspace(0, 1, 11):
            s = shape_amp(fA + q * (fB - fA))[1:] * k
            res.append((b.rms(20 * np.log10(H[:, 1] / s)), q))
        best = min(res)
        print(f"  A {gA} B {gB}: best q = {best[1]:.1f} ({best[0]:.2f} dB); rms at q=0: {res[0][0]:.2f}, 0.5: {res[5][0]:.2f}, 1: {res[10][0]:.2f}")


if __name__ == "__main__":
    main()

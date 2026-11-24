#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 The c2fx Authors
"""x04: the LSP field: bits 14..63, ten 5-bit Gray-coded indices, each into dlsp<j>.txt (Hz);
the LSPs are the running sum of the ten table values.

Part A: sweep each group with the others fixed; the spectral centroid moves monotonically with the
Gray-decoded index and group 0 moves the most, group 9 the least (a delta chain from 0 Hz).
Part B: for random speech-like frames, the measured harmonic levels (dB) against the model
-20log10|A(f)| computed from the cumulative table values via textbook LSP->LPC: slope ~1 means the
tables, units, group order and Gray coding are right.
"""
import numpy as np
import c2bb as b


def centroid(seg):
    S = np.abs(np.fft.rfft(seg * np.hanning(len(seg)), 8192)) ** 2
    f = np.fft.rfftfreq(8192, 1 / b.FS)
    return float((S * f).sum() / S.sum())


def main():
    print("table sizes (entries):", [len(t) for t in b.DLSP])
    print("tables 1-3, 7-10 are 25..800 Hz step 25; tables 4-6 are 25..200 step 25 then 250..1400 step 50:")
    for j in (0, 3):
        print("  dlsp%d:" % (j + 1), b.DLSP[j][:10], "...", b.DLSP[j][-3:])

    print("\nA. centroid (Hz) of the output vs decoded index 0,4,8,...,28 for each group (others at 8)")
    for grp in range(10):
        row = []
        for idx in range(0, 32, 4):
            g = [8] * 10
            g[grp] = idx
            seg = b.decode([b.make_frame(1, 1, 30, 20, g)] * 24)[2400:4000].astype(float)
            row.append(centroid(seg))
        print(f"  group {grp}:", " ".join(f"{x:6.0f}" for x in row))

    print("\nB. harmonic level vs model (random frames, f0 ~ 100..190 Hz, dB, per-frame offset removed)")
    rng = np.random.default_rng(5)
    X, Y, T = [], [], []
    for trial in range(40):
        wi = int(rng.integers(20, 50))
        f0 = b.w0_hz(wi)
        L = int(4000 // f0)
        while True:
            g = rng.integers(2, 20, 10)
            f = b.lsp_hz_from_indices(g)
            if 2800 < f[-1] < 3700:
                break
        seg = b.decode([b.make_frame(1, 1, wi, 22, g)] * 30)[2400:4400]
        a = b.lsp_to_lpc(f)
        fh = f0 * np.arange(1, L + 1)
        meas = 20 * np.log10([b.harm_amp(seg, x) + 1e-9 for x in fh])
        A1, A2 = b.aresp(a, fh), b.aresp(a, fh, 0.5)
        ok = (fh > 300) & (fh < 3400)
        for i in np.where(ok)[0]:
            Y.append(meas[i])
            X.append([-20 * np.log10(abs(A1[i])), 20 * np.log10(abs(A2[i]) / abs(A1[i])), 1.0 if fh[i] < 1000 else 0.0])
            T.append(trial)
    X, Y, T = np.array(X), np.array(Y), np.array(T)
    for t in set(T):
        m = T == t
        Y[m] -= Y[m].mean()
        X[m] -= X[m].mean(0)
    c = np.linalg.lstsq(X, Y, rcond=None)[0]
    print("  regression of measured dB on [-20log|A| , 20log|A(z/0.5)/A(z)| , (f<1000 Hz)]:", np.round(c, 3))
    print("  rms residual %.2f dB (before: %.2f dB)" % (b.rms(X @ c - Y), b.rms(Y)))
    c1 = np.linalg.lstsq(X[:, :1], Y, rcond=None)[0]
    print("  with only -20log|A|: coef %.3f residual %.2f dB" % (c1[0], b.rms(X[:, :1] @ c1 - Y)))


if __name__ == "__main__":
    main()

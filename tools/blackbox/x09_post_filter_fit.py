#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 The c2fx Authors
"""x09: fit the post-filter exponents once the rest of the envelope chain is known.

Chain (spec 6.1-6.3): 512-point power spectrum of 1/A (256 bins); post filter P' = P * |A(z/gamma)/A(z)|^(2*beta);
rescale so the sum over the 256 bins equals the sum before filtering; scale by the energy power; multiply bins 0..63
by 1.96; harmonic m amplitude^2 = sum of the bins from trunc((m-0.5)r+0.5) up to (not including)
min(trunc((m+0.5)r+0.5), 256), r = w0/(2*pi/512); harmonic 1 times 0.032 in amplitude when f0 < 150 Hz.

Everything is fixed except (gamma, beta) and ONE global level constant. The score is the rms (dB) of measured minus
predicted harmonic level over many random frames, with the single global constant removed (so a wrong shape and a
wrong normalisation both show up). Harmonics more than 40 dB under the frame's strongest are excluded (see score()). Prints the grid's best cells and the best cell's residual.
"""
import itertools
import numpy as np
import c2bb as b

NF = 512


def bins(a, gamma):
    return b.aresp(a, np.arange(NF // 2) * b.FS / NF, gamma)


def predict_db(f, f0, L, gamma, beta, bass=True):
    a = b.lsp_to_lpc(f)
    A1 = bins(a, 1.0)
    P0 = 1 / np.abs(A1) ** 2
    P = P0 * (np.abs(bins(a, gamma)) / np.abs(A1)) ** (2 * beta)
    P = P * (P0.sum() + 1e-4) / (P.sum() + 1e-4)
    if bass:
        P = P.copy()
        P[:64] *= 1.96
    r = f0 * NF / b.FS
    out = []
    for m in range(1, L + 1):
        lo = int(np.floor((m - 0.5) * r + 0.5))
        hi = min(int(np.floor((m + 0.5) * r + 0.5)), 256)
        out.append(P[lo:hi].sum() if hi > lo else 1e-12)
    pw = np.array(out)
    if f0 < 150:
        pw[0] *= 0.032 ** 2
    return 10 * np.log10(pw)


def collect(seed=21, widx=(10, 20, 30, 40, 50, 60, 75, 90), per=6, e=20):
    rng = np.random.default_rng(seed)
    data = []
    for wi in widx:
        f0 = b.w0_hz(wi)
        L = int(4000 // f0)
        for _ in range(per):
            while True:
                g = rng.integers(3, 18, 10)
                f = b.lsp_hz_from_indices(g)
                if 2800 < f[-1] < 3800:
                    break
            seg = b.decode([b.make_frame(1, 1, wi, e, g)] * 40)[3000:5400]
            amp = np.array([b.harm_amp(seg, m * f0) for m in range(1, L + 1)])
            data.append((f, f0, L, 20 * np.log10(amp + 1e-9)))
    return data


def score(data, gamma, beta, bass=True, floor_db=40.0):
    """Harmonics predicted more than floor_db below the frame's strongest are left out: the decoder gives
    harmonics under a background level random phases, so a steady-tone measurement of them is unreliable
    (their residual sd is 3-9 dB against 0.3-0.7 dB above the floor)."""
    parts = []
    for f, f0, L, meas in data:
        pr = predict_db(f, f0, L, gamma, beta, bass)
        parts.append((meas - pr)[pr >= pr.max() - floor_db])
    res = np.concatenate(parts)
    return b.rms(res - res.mean()), float(res.mean())


def main():
    data = collect()
    print(f"{len(data)} frames, {sum(len(d[3]) for d in data)} harmonics")
    grid = []
    for gamma in (0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9):
        for beta in (0.0, 0.1, 0.2, 0.3, 0.4, 0.5, 0.6):
            s, c = score(data, gamma, beta)
            grid.append((s, gamma, beta, c))
    grid.sort()
    print("best (rms dB, gamma, beta, global constant dB):")
    for r in grid[:8]:
        print("  %.3f  gamma %.2f beta %.2f const %.2f" % r)
    print("no post filter (beta 0) rms %.3f; without bass boost at the best cell: %.3f" % (score(data, 0.5, 0.0)[0], score(data, grid[0][1], grid[0][2], bass=False)[0]))
    c = grid[0][3]
    print("energy scale: at e = 20 the global constant is %.2f dB, so (E_MIN_DB + synthesis gain) = %.2f dB "
          "given 1.5625 dB per step (x03); the constant cannot be split between the two by output alone" % (c, c - 1.5625 * 20))
    # refine
    g0, b0 = grid[0][1], grid[0][2]
    fine = []
    for gamma in np.arange(max(0.1, g0 - 0.1), g0 + 0.101, 0.025):
        for beta in np.arange(max(0.0, b0 - 0.1), b0 + 0.101, 0.025):
            s, c = score(data, gamma, beta)
            fine.append((s, gamma, beta, c))
    fine.sort()
    print("refined (rms dB, gamma, beta, const):")
    for r in fine[:8]:
        print("  %.3f  gamma %.3f beta %.3f const %.2f" % r)
    # ridge: for each gamma the best beta and its rms
    print("best beta per gamma (rms):")
    for gamma in (0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9):
        best = min((score(data, gamma, bt)[0], bt) for bt in np.arange(0, 0.81, 0.05))
        print("  gamma %.1f -> beta %.2f rms %.3f" % (gamma, best[1], best[0]))


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) The c2fx Authors
"""x08: voicing bits, determinism, start-up and out-of-range indices (observations, not a full model).

A. Bits 0 and 1: which combinations decode as voiced (periodic) in steady state and in transitions.
B. The decoder is deterministic; unvoiced output depends on w0 (so w0 is not ignored when unvoiced).
C. A single frame decodes to 160 samples; level ramps up over the first frame (start-up state).
D. Extreme LSP indices (sum above 4 kHz) decode without error and stay bounded.
E. Pitch indices are always 0..127 (7 bits): every value decodes to a periodic voiced signal.
"""
import numpy as np
import c2bb as b

G = [10, 10, 10, 10, 12, 12, 12, 12, 14, 16]


def mk(v1, v2, w=55, e=22):
    return b.make_frame(v1, v2, w, e, G)


def block_periodicity(out, lag=40):
    res = []
    for k in range(len(out) // 80 - 1):
        best = 0.0
        for L in range(lag - 2, lag + 3):
            x = out[k * 80:k * 80 + 80 + L + 1].astype(float)
            a, c = x[:80], x[L:L + 80]
            best = max(best, float(np.sum(a * c) / (np.sqrt(np.sum(a * a) * np.sum(c * c)) + 1e-9)))
        res.append(round(best, 1))
    return res


def main():
    print("A. periodicity per 80-sample block (1.0 = periodic) for voicing patterns (v1,v2)")
    for name, pat in {
        "(1,1) x8": [(1, 1)] * 8,
        "(0,1) x8": [(0, 1)] * 8,
        "(1,0) x8": [(1, 0)] * 8,
        "(0,0) x8": [(0, 0)] * 8,
        "(1,1) x4 then (0,1) x4": [(1, 1)] * 4 + [(0, 1)] * 4,
        "(1,1) x4 then (1,0) x4": [(1, 1)] * 4 + [(1, 0)] * 4,
        "(0,0) x4 then (1,1) x4": [(0, 0)] * 4 + [(1, 1)] * 4,
        "(0,0) x2 (1,1) (0,0) x3": [(0, 0)] * 2 + [(1, 1)] + [(0, 0)] * 3,
    }.items():
        print(f"  {name:28s}", block_periodicity(b.decode([mk(*p) for p in pat])))

    print("\nB. determinism and w0 dependence when unvoiced")
    o1 = b.decode([mk(0, 0, 100)] * 12)
    o2 = b.decode([mk(0, 0, 100)] * 12)
    o3 = b.decode([mk(0, 0, 5)] * 12)
    o4 = b.decode([mk(1, 0, 100)] * 12)
    print("  same input twice identical:", np.array_equal(o1, o2))
    print("  (0,0) w0=100 vs w0=5 identical:", np.array_equal(o1, o3))
    print("  (0,0) vs (1,0) identical:", np.array_equal(o1, o4))

    print("\nC. start-up: rms per 40-sample block of the first 3 identical frames")
    o = b.decode([mk(1, 1, 50, 20)] * 3)
    print("  ", [int(b.rms(o[k * 40:(k + 1) * 40])) for k in range(12)], " length of a 1-frame decode:", len(b.decode([mk(1, 1)])))

    print("\nD. extreme LSP indices")
    for g in ([31] * 10, [0] * 10, [31, 0] * 5):
        o = b.decode([b.make_frame(1, 1, 50, 20, g)] * 30)
        print(f"  indices {g[:4]}...: cumulative LSP (Hz) {[int(x) for x in b.lsp_hz_from_indices(g)]}, rms {b.rms(o[3000:5000]):.0f}, max |x| {int(np.abs(o).max())}")

    print("\nE. every pitch index decodes voiced and periodic (autocorrelation peak, steady state)")
    bad = []
    for i in range(0, 128):
        o = b.decode([b.make_frame(1, 1, i, 20, G)] * 30)
        seg = o[2400:4000].astype(float)
        f = b.f0_of(seg, 40, 500)
        if abs(f - b.w0_hz(i)) > 0.06 * b.w0_hz(i) and abs(f - b.w0_hz(i) / 2) > 0.06 * b.w0_hz(i) / 2:
            bad.append((i, round(b.w0_hz(i), 1), round(f, 1)))
    print("  indices whose autocorrelation pitch is not f0 or f0/2 within 6%:", bad)


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 The c2fx Authors
"""x03: the energy field: bits 9..13, Gray coded, 1.5625 dB per step, and how it sets the output level.

Part A: sweep the 32 values with everything else fixed, measure output rms (steady state).
Part B: with a fixed index, the total harmonic power of the output against the 512-point band sum of
|1/A|^2 (A from the dequantised LSPs): the offset is constant across w0 and across envelopes, which
fixes the scale between the energy index and the envelope.
"""
import numpy as np
import c2bb as b


def main():
    G = [10] * 10
    print("A. energy index (decoded) vs output rms and dB")
    prev = None
    rows = []
    for e in range(32):
        out = b.decode([b.make_frame(1, 1, 30, e, G)] * 30)
        r = b.rms(out[2400:4000])
        rows.append((e, r))
        print(f"  e={e:2d} rms={r:8.1f}  dB={20 * np.log10(r):6.2f}" + ("" if prev is None else f"  step {20 * np.log10(r / prev):5.2f}"))
        prev = r
    es = np.array([x[0] for x in rows[3:24]])
    db = np.array([20 * np.log10(x[1]) for x in rows[3:24]])
    print("  slope over e=3..23 (unclipped):", np.polyfit(es, db, 1)[0], "dB/step")

    print("\nB. offset of 10log10(sum a_m^2) over 10log10(sum over harmonic bands of |1/A(k)|^2, 512-pt grid)")
    rng = np.random.default_rng(12)
    for wi in (5, 20, 30, 55, 80, 110, 127):
        f0 = b.w0_hz(wi)
        L = int(4000 // f0)
        offs, offp = [], []
        for _ in range(10):
            while True:
                g = rng.integers(3, 18, 10)
                f = b.lsp_hz_from_indices(g)
                if 2800 < f[-1] < 3800:
                    break
            a = b.lsp_to_lpc(f)
            seg = b.decode([b.make_frame(1, 1, wi, 20, g)] * 40)[3000:5400]
            pw = np.array([b.harm_amp(seg, m * f0) ** 2 for m in range(1, L + 1)])
            # bins 0..256 (257 bins, Nyquist included); a band is clipped to this range
            Hk = 1 / np.abs(b.aresp(a, np.arange(257) * b.FS / 512)) ** 2
            r = f0 * 512 / b.FS
            bs = np.array([Hk[int(np.floor((m - 0.5) * r + 0.5)):min(int(np.floor((m + 0.5) * r + 0.5)), 257)].sum() for m in range(1, L + 1)])
            pt = np.array([Hk[min(int(np.floor(m * r + 0.5)), 256)] for m in range(1, L + 1)])   # point sample at the harmonic's bin
            s = 1 if f0 < 150 else 0   # first harmonic is removed below 150 Hz (x06)
            offs.append(10 * np.log10(pw[s:].sum()) - 10 * np.log10(bs[s:].sum()))
            offp.append(10 * np.log10(pw[s:].sum()) - 10 * np.log10(pt[s:].sum()))
        print(f"  w0 idx {wi:3d} f0 {f0:6.1f} L {L:2d}: band-sum offset mean {np.mean(offs):6.2f} dB sd {np.std(offs):4.2f}; point-sample offset mean {np.mean(offp):6.2f}")


if __name__ == "__main__":
    main()

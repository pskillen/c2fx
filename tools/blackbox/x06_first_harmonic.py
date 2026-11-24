#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 The c2fx Authors
"""x06: the first harmonic is removed when f0 is low.

Decode steady frames for w0 indices 30..42 with several envelopes and energies; print the level of
harmonic 1 (and 2, 3) relative to the model, after a per-frame offset taken from the mid band.
"""
import numpy as np
import c2bb as b


def main():
    for g in ([10, 10, 10, 10, 12, 12, 12, 12, 14, 16], [20, 8, 8, 8, 8, 8, 8, 8, 8, 8], [3, 9, 9, 9, 9, 9, 9, 9, 9, 9]):
        f = b.lsp_hz_from_indices(g)
        a = b.lsp_to_lpc(f)
        print("LSP (Hz)", [int(x) for x in f])
        for e in (10, 22):
            for wi in range(33, 41):
                f0 = b.w0_hz(wi)
                L = int(4000 // f0)
                seg = b.decode([b.make_frame(1, 1, wi, e, g)] * 40)[3000:5000]
                fh = f0 * np.arange(1, L + 1)
                meas = 20 * np.log10([b.harm_amp(seg, x) + 1e-9 for x in fh])
                A1, A2 = b.aresp(a, fh), b.aresp(a, fh, 0.5)
                pred = -20 * np.log10(abs(A1)) + 0.2 * 20 * np.log10(abs(A2) / abs(A1)) + 3.0 * (fh < 1000)
                d = meas - pred
                d -= np.median(d[(fh > 1200) & (fh < 2500)])
                print(f"  e {e:2d} idx {wi} f0 {f0:6.1f}: harmonics 1-3 relative level (dB) {np.round(d[:3], 1)}")


if __name__ == "__main__":
    main()

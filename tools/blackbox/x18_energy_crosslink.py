#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 The c2fx Authors
"""x18: does the encoder's energy scale (x12) explain the decoder's level offset (x09)?

Part A first checks the quantized energy column of the committed vectors/*_params.csv against the scale.

The encoder reconstructs energy index e as 10*log10(E) = -10 + e * 50/32 dB (x12), E being the LPC gain squared of its
analysis window. If the decoder's energy dB scale is the same one, then E_MIN_DB = -10 dB and the decoder's level offset
at e = 20 (x09) is  E_MIN_DB + 20 * 1.5625 + G = 21.25 dB + G, where G is the synthesis gain in dB (amplitude of a
decoded sinusoid against the square root of the band power). Candidate: a real sinusoid synthesised from a one-sided
spectrum of complex amplitude A has peak amplitude 2*A, i.e. G = 20*log10(2) = 6.02 dB, which predicts an offset of 27.27 dB.

Prints, for the best pair and the PDF pair of x09, G implied by their measured offsets; then refits (gamma, beta) with the
offset FIXED at 27.27 dB (no per-fit constant) and prints the best cells and their rms, to compare with x09's 0.737 dB
(free constant). Same frames, same chain, same exclusion of harmonics more than 40 dB under the strongest.
"""
import glob
import os
import numpy as np
import c2bb as b
import x09_post_filter_fit as x9

E_MIN_DB = -10.0          # x12: index 0 is -10 dB
STEP = 50.0 / 32.0
G_SINE = 20 * np.log10(2.0)


def score_fixed(data, gamma, beta, offset):
    parts = []
    for f, f0, L, meas in data:
        pr = x9.predict_db(f, f0, L, gamma, beta)
        parts.append((meas - pr - offset)[pr >= pr.max() - 40.0])
    return b.rms(np.concatenate(parts))


def vector_energies():
    """Part A: the quantized energy column of the committed vectors/*_params.csv (c2sim's own record after the 3200
    quantizers) against 10^((-10 + e*50/32)/10), e = 0..31. Returns (rows, worst relative error, indices used)."""
    here = os.path.dirname(os.path.abspath(__file__))
    files = sorted(glob.glob(os.path.join(here, "..", "..", "vectors", "*_params.csv")))
    levels = np.array([10 ** ((E_MIN_DB + e * STEP) / 10) for e in range(32)])
    n, worst, used = 0, 0.0, set()
    for f in files:
        for line in open(f).read().splitlines()[1:]:
            v = float(line.split(",")[3])
            e = int(np.argmin(np.abs(np.log(levels) - np.log(v)))) if v > 0 else 0
            worst = max(worst, abs(v / levels[e] - 1))
            used.add(e)
            n += 1
    return len(files), n, worst, sorted(used)


def main():
    nf, n, worst, used = vector_energies()
    print("A. %d vector files, %d sub-frame energies: worst relative error against 10^((-10+e*50/32)/10) is %.2e; "
          "indices used %s" % (nf, n, worst, used))
    print("   (float32 values; E_MIN_DB = -10 dB, step 1.5625 dB, so E_MAX_DB = 40 dB)")
    data = x9.collect()
    base = E_MIN_DB + 20 * STEP
    print("encoder scale: E_MIN_DB = %.2f dB; E_MIN_DB + 20 steps = %.2f dB; G for a doubled one-sided sinusoid = %.2f dB"
          % (E_MIN_DB, base, G_SINE))
    print("predicted offset at e=20 for G = %.2f dB: %.2f dB" % (G_SINE, base + G_SINE))
    for name, gamma, beta in (("x09 refined best", 0.6, 0.275), ("PDF pair", 0.5, 0.2)):
        s, c = x9.score(data, gamma, beta)
        print("  %-17s gamma %.3f beta %.3f free-constant rms %.3f dB, offset %.2f dB, implied G = %.2f dB"
              % (name, gamma, beta, s, c, c - base))
    off = base + G_SINE
    grid = []
    for gamma in np.arange(0.3, 0.91, 0.05):
        for beta in np.arange(0.0, 0.61, 0.025):
            grid.append((score_fixed(data, gamma, beta, off), gamma, beta))
    grid.sort()
    print("offset fixed at %.2f dB: best cells (rms dB, gamma, beta)" % off)
    for r in grid[:6]:
        print("  %.3f  gamma %.3f beta %.3f" % r)
    print("  PDF pair under the fixed offset: %.3f dB" % score_fixed(data, 0.5, 0.2, off))


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 The c2fx Authors
"""x10: the pitch of the first sub-frame (a) of a frame, from the previous and the current frame.

Method: steady voiced frames at pitch index A, then steady frames at B (flat-ish envelope, so many strong harmonics).
Take a short Hann window (80 samples, 100 Hz resolution) centred on the middle of sub-frame a of the first B frame
(where its own triangular synthesis window peaks), and find the f0 whose harmonic comb gets the most energy
(a harmonic sieve over harmonics 1..L up to 3.8 kHz). The same is done on steady A and steady B for calibration.
Compared: copy of previous f0, copy of current f0, midpoint of the two f0 values.
"""
import numpy as np
import c2bb as b

G = [10] * 10


def sieve(x, centre, half=60, lo=60, hi=420):
    """f0 maximising the contrast between spectrum at the harmonics and halfway between them."""
    seg = np.asarray(x[centre - half:centre + half], float) * np.hanning(2 * half)
    nfft = 4096
    S = np.abs(np.fft.rfft(seg, nfft)) ** 2
    df = b.FS / nfft
    best = (-1, 0)
    for f0 in np.arange(lo, hi, 0.5):
        m = np.arange(1, int(3600 // f0) + 1)
        on = S[np.round(m * f0 / df).astype(int)].sum()
        off = S[np.round((m - 0.5) * f0 / df).astype(int)].sum() + 1e-9 * on + 1e-12
        v = on / off
        if v > best[0]:
            best = (v, f0)
    return best[1]


def main():
    print("A. voiced -> voiced. Sieve f0 (Hz) at the centre of sub-frame a of the first new frame")
    print("  A->B idx  prevHz  curHz  midHz | steadyA steadyB | sub-frame a | nearest rule")
    for ia, ib in ((30, 70), (70, 30), (40, 80), (80, 40), (50, 100), (100, 50)):
        fa, fb = b.w0_hz(ia), b.w0_hz(ib)
        fr = [b.make_frame(1, 1, ia, 22, G)] * 8 + [b.make_frame(1, 1, ib, 22, G)] * 6
        out = b.decode(fr)
        k = 8 * 160
        sa = sieve(out, k - 160 * 2 + 80)    # steady A
        sb = sieve(out, k + 160 * 3 + 80)    # steady B
        got = sieve(out, k + 80)             # sub-frame a of the first B frame
        mid = (fa + fb) / 2
        near = min((abs(got - fa), "prev"), (abs(got - fb), "cur"), (abs(got - mid), "mid"))[1]
        print(f"  {ia:3d}->{ib:3d}  {fa:6.1f} {fb:6.1f} {mid:6.1f} | {sa:6.1f} {sb:6.1f} | {got:6.1f} | {near}")

    print("\nB. start-up: sub-frame a of the very first frame (previous state = initial), first frame voiced (1,1)")
    print("  idx  curHz  | got Hz | midpoint with 50 Hz | copy of current")
    for i in (40, 70, 100, 120):
        out = b.decode([b.make_frame(1, 1, i, 22, G)] * 5)
        got = sieve(out, 80)
        f = b.w0_hz(i)
        print(f"  {i:3d}  {f:6.1f} | {got:6.1f} | {(f + 50) / 2:6.1f}             | {f:6.1f}")

    print("\nC. unvoiced (0,0) frames at index A, then voiced (1,1) at index B: sieve f0 at the centre of sub-frame a")
    print("  A->B     curHz  midHz | sub-frame a | steadyB | nearest rule")
    for ia, ib in ((30, 70), (70, 30), (40, 80), (100, 50)):
        fa, fb = b.w0_hz(ia), b.w0_hz(ib)
        out = b.decode([b.make_frame(0, 0, ia, 22, G)] * 8 + [b.make_frame(1, 1, ib, 22, G)] * 6)
        k = 8 * 160
        got, sb = sieve(out, k + 80), sieve(out, k + 160 * 3 + 80)
        near = min((abs(got - fb), "cur"), (abs(got - (fa + fb) / 2), "mid"), (abs(got - fa), "prev"))[1]
        print(f"  {ia:3d}->{ib:3d} {fb:6.1f} {(fa + fb) / 2:6.1f} | {got:6.1f} | {sb:6.1f} | {near}")


if __name__ == "__main__":
    main()

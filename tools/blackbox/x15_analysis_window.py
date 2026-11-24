#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 The c2fx Authors
"""x15: the analysis window that the dumped LPC energy comes from.

10 ms row s is a 279-point Hann window centred on input sample (s-1)*80
(row 0 is centred 80 samples before the first input sample). The weights are

    w[i] = 0.5 - 0.5*cos(2*pi*i/278),  i = 0..278

scaled so that sum(w^2) = 1/512. The dumped energy (pitch-dump column 4) equals
the order-10 autocorrelation LPC gain of that windowed signal for an impulse,
and to a fraction of a percent for a pulse-train vowel. A pure low-frequency
sine does not: its dumped energy stays finite while this gain collapses.
"""
import numpy as np
import c2bb as b


def window():
    i = np.arange(279)
    w = 0.5 - 0.5 * np.cos(2 * np.pi * i / 278)
    return w * np.sqrt((1.0 / 512.0) / np.sum(w * w))


W = window()


def lpc_gain(x, order=10):
    """Residual energy of the autocorrelation method. x is already windowed."""
    x = np.asarray(x, float)
    n = len(x)
    if np.dot(x, x) <= 0:
        return 0.0
    r = np.correlate(x, x, "full")[n - 1:n + order]
    a = np.zeros(order + 1)
    a[0] = 1.0
    e = float(r[0])
    for i in range(1, order + 1):
        acc = float(r[i] + np.dot(a[1:i], r[i - 1:0:-1]))
        k = -acc / e if e else 0.0
        a[1:i] = a[1:i] + k * a[i - 1:0:-1]
        a[i] = k
        e *= 1.0 - k * k
    return float(e)


def gain_at(pcm, centre):
    seg = np.zeros(279)
    start = centre - 139
    for i in range(279):
        j = start + i
        if 0 <= j < len(pcm):
            seg[i] = pcm[j]
    return lpc_gain(seg * W)


def main():
    print(f"sum of squared weights {np.sum(W * W):.6f} (target 1/512 = {1/512:.6f})")
    print("\nimpulse of 20000: dumped energy vs LPC gain, centre = (s-1)*80")
    print("pos   row  centre   dumped      gain     ratio")
    for pos in (0, 400, 720, 800, 801):
        pcm = np.zeros(2400, np.int16)
        pcm[pos] = 20000
        _frames, pitch, _lsp = b.analyse(pcm)
        for s, row in enumerate(pitch):
            if row[3] < 1.0 and not (pos - 200 <= (s - 1) * 80 <= pos + 200):
                continue
            if row[3] < 1.0:
                continue
            centre = (s - 1) * 80
            g = gain_at(pcm.astype(float), centre)
            ratio = row[3] / g if g else float("inf")
            print(f"{pos:4d}  {s:3d}  {centre:6d}  {row[3]:10.2f}  {g:10.2f}  {ratio:.5f}")

    print("\npulse-train vowel, odd rows of frames 6..14")
    pcm = b.voiced_pulses(150, 0.5, 8000)
    _frames, pitch, _lsp = b.analyse(pcm)
    ratios = []
    for k in range(6, 15):
        s = 2 * k + 1
        g = gain_at(pcm.astype(float), (s - 1) * 80)
        ratios.append(pitch[s][3] / g)
    print(f"  dumped/gain  min {min(ratios):.5f}  max {max(ratios):.5f}  mean {np.mean(ratios):.5f}")

    print("\n20 Hz sine, amplitude 8000: dumped energy is not this gain")
    t = np.arange(4000)
    sine = np.clip(np.round(8000 * np.sin(2 * np.pi * 20 * t / b.FS)), -32767, 32767).astype(np.int16)
    _frames, pitch, _lsp = b.analyse(sine)
    print("  row   dumped     gain")
    for s in range(10, 16):
        g = gain_at(sine.astype(float), (s - 1) * 80)
        print(f"  {s:3d}  {pitch[s][3]:8.3f}  {g:8.4f}")

    print("\nclamping the reflection coefficient, or boosting r[0], does not recover row 10")
    centre = 9 * 80
    seg = np.zeros(279)
    start = centre - 139
    x = sine.astype(float)
    for i in range(279):
        j = start + i
        if 0 <= j < len(x):
            seg[i] = x[j]
    seg = seg * W
    dumped = pitch[10][3]

    def gain_limited(kmax=1.0, r0_scale=1.0, order=10):
        r = np.correlate(seg, seg, "full")[278:279 + order]
        r = np.array(r, float)
        r[0] *= r0_scale
        a = np.zeros(order + 1)
        a[0] = 1.0
        e = float(r[0])
        for i in range(1, order + 1):
            acc = float(r[i] + np.dot(a[1:i], r[i - 1:0:-1]))
            k = -acc / e if e else 0.0
            k = float(np.clip(k, -kmax, kmax))
            a[1:i] = a[1:i] + k * a[i - 1:0:-1]
            a[i] = k
            e *= 1.0 - k * k
        return float(e)

    print(f"  dumped {dumped:.3f}")
    for kmax in (1.0, 0.999, 0.99, 0.95):
        print(f"  |k| <= {kmax:<5}  gain {gain_limited(kmax):.3f}")
    for scale in (1.0001, 1.001, 1.01):
        print(f"  r[0] * {scale:<6}  gain {gain_limited(1.0, scale):.3f}")


def per_file():
    """The G^2 of the window above, through the energy quantizer, against the index c2enc writes (frame 2k+1)."""
    import os
    root = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
    print("\nG^2 of this window through the energy quantizer, against the index written, per test file")
    print(f"{'file':<28} {'frames':>6} {'exact':>6} {'within1':>8}  dumped E vs G^2 on frame 20 (or the last)")
    tot = ex_t = w1_t = 0
    for folder in ("voice", "signals"):
        d = os.path.join(root, "tests", "data", folder)
        for name in sorted(os.listdir(d)):
            if not name.endswith(".pcm"):
                continue
            pcm = np.fromfile(os.path.join(d, name), dtype="<i2")
            frames, pitch, _lsp = b.analyse(pcm)
            ex = w1 = n = 0
            for k, fr in enumerate(frames):
                s = 2 * k + 1
                if s >= len(pitch):
                    break
                g = gain_at(pcm.astype(float), (s - 1) * 80)
                got = b.quant_energy(g)
                ex += got == fr["e"]
                w1 += abs(got - fr["e"]) <= 1
                n += 1
            s = min(41, len(pitch) - 1)
            g = gain_at(pcm.astype(float), (s - 1) * 80)
            print(f"{name:<28} {n:6d} {ex:6d} {w1:8d}  {pitch[s][3]:.4g} vs {g:.4g}")
            tot += n
            ex_t += ex
            w1_t += w1
    print(f"{'ALL':<28} {tot:6d} {ex_t:6d} {w1_t:8d}  ({100.0 * w1_t / tot:.0f}% within 1)")


if __name__ == "__main__":
    main()
    per_file()

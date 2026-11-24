#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 The c2fx Authors
"""x11: how c2enc picks the 7-bit w0 index from the analysis pitch.

Rule under test: the index is the nearest point of f0 = 50 + 350*i/128 Hz,
i = clip(round((f0-50)*128/350), 0, 127), and f0 is the analysis pitch of the
second 10 ms frame in the codec frame (c2sim --dump_pitch_e, odd row).
An analysis of 400 Hz is raw index 128 and is clipped to 127.
"""
import numpy as np
import c2bb as b


def main():
    print("pulse trains: bitstream index vs nearest grid point of the odd 10 ms analysis pitch")
    print("f0   frame  analysis_Hz  raw_index  got  nearest")
    miss = 0
    half = 1.0
    n = 0
    for f0 in (60, 90, 110, 140, 175, 210, 260, 330, 380):
        frames, pitch, _lsp = b.analyse(b.voiced_pulses(f0, 0.6, 9000))
        for k in (4, 8, 12):
            f = pitch[2 * k + 1][0] * b.FS / (2 * np.pi)
            raw = (f - 50.0) * 128.0 / 350.0
            got = frames[k]["w"]
            pred = b.quant_w0(pitch[2 * k + 1][0])
            half = min(half, abs(raw - np.floor(raw) - 0.5))
            n += 1
            miss += got != pred
            if k == 8:
                print(f"{f0:3.0f}  {k:5d}  {f:11.2f}  {raw:9.3f}  {got:3d}  {pred:3d}")
    print(f"mismatches {miss}/{n}; closest raw index came within {half:.3f} of a midpoint")

    print("\npulse rates at the top of the range: a raw index of 128 clips to 127; a higher rate is not always estimated as 400 Hz")
    for f0 in (400, 450, 520):
        frames, pitch, _lsp = b.analyse(b.voiced_pulses(f0, 0.5, 9000))
        k = 8
        f = pitch[2 * k + 1][0] * b.FS / (2 * np.pi)
        raw = (f - 50.0) * 128.0 / 350.0
        print(f"  pulse {f0:3d} Hz -> analysis {f:6.2f} Hz, raw index {raw:6.2f}, bits {frames[k]['w']}")

    print("\nglide 80->320 Hz: which 10 ms row the codec frame's index follows")
    n = 8000
    x = np.zeros(n)
    pos = 0.0
    while pos < n - 1:
        x[int(pos)] = 1.0
        pos += b.FS / (80 + 240 * pos / n)
    y = x.copy()
    for fc, bw in ((700, 100), (1500, 140), (2600, 200)):
        r = np.exp(-np.pi * bw / b.FS)
        th = 2 * np.pi * fc / b.FS
        a1, a2 = -2 * r * np.cos(th), r * r
        z = np.zeros(n)
        for i in range(n):
            z[i] = y[i] - a1 * (z[i - 1] if i else 0) - a2 * (z[i - 2] if i > 1 else 0)
        y = z
    y = np.clip(np.round(y / np.max(np.abs(y)) * 9000), -32767, 32767).astype(np.int16)
    frames, pitch, _lsp = b.analyse(y)
    hit = [0, 0]
    for k in range(2, len(frames) - 1):
        for which, row in ((0, 2 * k), (1, 2 * k + 1)):
            if frames[k]["w"] == b.quant_w0(pitch[row][0]):
                hit[which] += 1
    print(f"  index matches even row {hit[0]}, odd row {hit[1]}, of {len(frames) - 3} frames")


if __name__ == "__main__":
    main()

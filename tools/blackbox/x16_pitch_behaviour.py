#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 The c2fx Authors
"""x16: what fundamental the encoder actually estimates.

Pulse trains in the 50-400 Hz design range come back near the pulse rate.
Pure tones do not: the analysis pitch is not the tone frequency. A 600 Hz
pulse train, above the design range, comes back near a third of the rate.
The second column of the pitch dump is printed because pure tones are voiced
while that column sits near zero, so it is not a 6 dB voicing gate.
"""
import numpy as np
import c2bb as b


def tone(f, n=4800, amp=12000):
    t = np.arange(n)
    return np.clip(np.round(amp * np.sin(2 * np.pi * f * t / b.FS)), -32767, 32767).astype(np.int16)


def main():
    print("pulse trains: analysis pitch of 10 ms row 17 (codec frame 8) against the pulse rate")
    print("rate   analysis   ratio  index")
    off = 0
    ratios = []
    for f0 in range(55, 391, 15):
        frames, pitch, _lsp = b.analyse(b.voiced_pulses(f0, 0.45, 9000))
        f = pitch[17][0] * b.FS / (2 * np.pi)
        ratio = f / f0
        ratios.append(ratio)
        if ratio < 0.75 or ratio > 1.25:
            off += 1
        print(f"{f0:4d}  {f:8.1f}  {ratio:5.3f}  {frames[8]['w']:3d}")
    print(f"  rates 55,70,...,385 Hz with analysis outside 0.75..1.25 of the rate: {off}")
    print(f"  ratio range over all {len(ratios)} rates: {min(ratios):.3f} .. {max(ratios):.3f}; "
          f"inside 0.99..1.01: {sum(0.99 <= r <= 1.01 for r in ratios)} of {len(ratios)}")

    print("\npulse trains above the 400 Hz end of the range")
    for f0 in (420, 500, 600):
        _frames, pitch, _lsp = b.analyse(b.voiced_pulses(f0, 0.45, 9000))
        f = pitch[17][0] * b.FS / (2 * np.pi)
        print(f"  {f0:3d} Hz pulses -> analysis {f:6.1f} Hz")

    print("\npure tones, amplitude 12000, same row")
    print("tone   analysis   index  voiced  col2")
    for f in range(50, 401, 25):
        frames, pitch, _lsp = b.analyse(tone(f))
        row = pitch[17]
        hz = row[0] * b.FS / (2 * np.pi)
        print(f"{f:4d}  {hz:8.1f}  {frames[8]['w']:5d}  {int(round(row[2]))}  {row[1]:7.2f}")


if __name__ == "__main__":
    main()

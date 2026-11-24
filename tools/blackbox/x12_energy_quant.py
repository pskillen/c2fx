#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 The c2fx Authors
"""x12: how c2enc picks the 5-bit energy index from the analysis energy.

Rule under test: e = clip(round((10*log10(E) + 10) * 32/50), 0, 31),
E the analysis energy of the second 10 ms frame (pitch-dump column 4).
Index 0 reconstructs to 0.1 and the step is 1.5625 dB. E <= 0 gives index 0.
"""
import numpy as np
import c2bb as b


def blocks(n=8000):
    """Loud and quiet pulse trains, alternating every 640 samples, same pitch."""
    loud = b.voiced_pulses(140, n / b.FS, 14000).astype(float)
    quiet = b.voiced_pulses(140, n / b.FS, 700).astype(float)
    y = np.empty(n)
    for start in range(0, n, 640):
        src = loud if (start // 640) % 2 == 0 else quiet
        y[start:start + 640] = src[start:start + 640]
    return np.clip(np.round(y), -32767, 32767).astype(np.int16)


def main():
    print("steady pulse train: index vs (10*log10(E)+10)*32/50 of the odd 10 ms energy")
    print("amp    E        dB      raw    got")
    miss = 0
    n = 0
    half = 1.0
    for amp in (80, 250, 700, 2000, 6000, 16000, 28000):
        frames, pitch, _lsp = b.analyse(b.voiced_pulses(150, 0.5, amp))
        k = 8
        E = pitch[2 * k + 1][3]
        raw = -99.0 if E <= 0 else (10 * np.log10(E) + 10) * 32 / 50
        got = frames[k]["e"]
        pred = b.quant_energy(E)
        if E > 0:
            half = min(half, abs(raw - np.floor(raw) - 0.5))
        n += 1
        miss += got != pred
        db = float("-inf") if E <= 0 else 10 * np.log10(E)
        print(f"{amp:5d}  {E:8.2f}  {db:7.2f}  {raw:6.2f}  {got:3d}  pred {pred}")
    print(f"mismatches {miss}/{n}; closest positive raw index came within {half:.3f} of a midpoint")

    print("\nends: silence -> 0, full-scale pulses -> 31")
    for name, pcm in (
        ("silence", np.zeros(4000, np.int16)),
        ("full", b.voiced_pulses(140, 0.5, 32000)),
    ):
        frames, pitch, _lsp = b.analyse(pcm)
        ks = range(4, len(frames) - 1)
        got = sorted({frames[k]["e"] for k in ks})
        raws = []
        for k in ks:
            E = pitch[2 * k + 1][3]
            if E > 0:
                raws.append((10 * np.log10(E) + 10) * 32 / 50)
        if raws:
            span = f"raw index {min(raws):.2f} .. {max(raws):.2f}"
        else:
            span = "energy non-positive"
        print(f"  {name}: indices {got}, {span}")

    print("\nloud/quiet blocks of 640 samples: which 10 ms row the index follows")
    frames, pitch, _lsp = b.analyse(blocks())
    hit = [0, 0]
    differ = 0
    for k in range(2, len(frames) - 1):
        preds = [b.quant_energy(pitch[2 * k + which][3]) for which in (0, 1)]
        if preds[0] != preds[1]:
            differ += 1
        for which in (0, 1):
            if preds[which] == frames[k]["e"]:
                hit[which] += 1
    print(f"  matches even {hit[0]}, odd {hit[1]}, of {len(frames) - 3}; rows disagreed on {differ} frames")


if __name__ == "__main__":
    main()

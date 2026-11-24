#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 The c2fx Authors
"""x13: how c2enc picks the ten LSP difference indices.

Rule under test, on the unquantized LSPs of the second 10 ms frame (lspEWov,
radians, converted as Hz = rad * 8000 / (2*pi)): walk j = 1..10, choose the
table entry nearest in Hz to (f_j - sum of entries already chosen), then add
that entry to the sum. An independent nearest-neighbour on the unquantized
differences is the comparison that fails.
A target below the first table entry (25 Hz) selects index 0.
"""
import os
import numpy as np
import c2bb as b

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))


def open_loop(lsp_rad):
    hz = np.asarray(lsp_rad, float) * b.FS / (2 * np.pi)
    d = np.empty(10)
    d[0] = hz[0]
    d[1:] = np.diff(hz)
    return [int(np.argmin(np.abs(b.DLSP[j] - d[j]))) for j in range(10)]


def score(pcm, skip=2):
    frames, _pitch, lsp = b.analyse(pcm)
    closed = open_ = 0
    below = below_ok = 0
    below_t = []
    above = 0
    near = near_ok = 0
    n = 0
    for k in range(skip, len(frames) - 1):
        row = lsp[2 * k + 1]
        got = frames[k]["lsp"]
        closed += b.quant_lsp(row[:10]) == got
        open_ += open_loop(row[:10]) == got
        hz = np.asarray(row[:10]) * b.FS / (2 * np.pi)
        acc = 0.0
        for j in range(10):
            target = hz[j] - acc
            table = b.DLSP[j]
            dist = np.abs(table - target)
            order = np.argsort(dist)
            if target < table[0]:
                below += 1
                below_ok += got[j] == 0
                below_t.append(target)
            if target > table[-1]:
                above += 1
            if dist[order[1]] - dist[order[0]] < 0.05:
                near += 1
                near_ok += got[j] == int(order[0])
            acc += float(table[got[j]])
        n += 1
    return n, closed, open_, below, below_ok, near, near_ok, below_t, above


def main():
    print("closed-loop nearest (quantized running sum) vs open-loop nearest (unquantized differences)")
    print(f"{'signal':<22} {'n':>4} {'closed':>7} {'open':>6} {'below':>6} {'below->0':>8} {'near-mid':>8} {'nearer':>7}")
    signals = [
        ("pulses 140 Hz", b.voiced_pulses(140, 0.8, 8000)),
        ("pulses 90 Hz", b.voiced_pulses(90, 0.8, 10000)),
        ("pulses 220 Hz", b.voiced_pulses(220, 0.8, 6000)),
        ("noise", np.random.default_rng(1).integers(-8000, 8000, 6400, dtype=np.int16)),
        ("80 Hz sine", np.clip(np.round(12000 * np.sin(2 * np.pi * 80 * np.arange(6400) / b.FS)), -32767, 32767).astype(np.int16)),
    ]
    for name in ("high_voice", "low_voice", "test_transmission"):
        path = os.path.join(ROOT, "tests", "data", "voice", name + ".pcm")
        signals.append((name, np.fromfile(path, dtype="<i2")))
    all_below, all_above, tot_near, tot_ok = [], 0, 0, 0
    for name, pcm in signals:
        n, closed, open_, below, below_ok, near, near_ok, bt, above = score(pcm)
        print(f"{name:<22} {n:4d} {closed:7d} {open_:6d} {below:6d} {below_ok:8d} {near:8d} {near_ok:7d}")
        all_below += bt
        all_above += above
        tot_near += near
        tot_ok += near_ok
    print(f"targets below the first table entry (25 Hz): {len(all_below)}, from {min(all_below):.2f} to {max(all_below):.2f} Hz; "
          f"of those with target < 5 Hz: {sum(t < 5 for t in all_below)}; targets above the last entry: {all_above}")
    print(f"targets within 0.05 Hz of equal distance to two entries, all signals: {tot_near}, took the nearer entry: {tot_ok}")


if __name__ == "__main__":
    main()

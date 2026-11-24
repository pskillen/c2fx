#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 The c2fx Authors
"""x14: the four field rules on every PCM file under tests/data.

For codec frame k, compared with c2sim --rate 3200:
  w0 index, energy index, ten LSP indices  <-  10 ms row 2k+1
  voicing bit 0                            <-  voicing of row 2k
  voicing bit 1                            <-  voicing of row 2k+1
Rows are counted from k = 0. A second tally skips the first two frames.
"""
import os
import numpy as np
import c2bb as b

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))


def files():
    found = []
    for folder in ("voice", "signals"):
        d = os.path.join(ROOT, "tests", "data", folder)
        for name in sorted(os.listdir(d)):
            if name.endswith(".pcm"):
                found.append(os.path.join(d, name))
    return found


def tally(frames, pitch, lsp, k0):
    w = e = lsp_ok = v = 0
    w_even = e_even = 0
    n = 0
    for k in range(k0, len(frames)):
        if 2 * k + 1 >= len(pitch) or 2 * k + 1 >= len(lsp):
            break
        odd, even = pitch[2 * k + 1], pitch[2 * k]
        fr = frames[k]
        w += fr["w"] == b.quant_w0(odd[0])
        e += fr["e"] == b.quant_energy(odd[3])
        lsp_ok += fr["lsp"] == b.quant_lsp(lsp[2 * k + 1][:10])
        v += fr["v0"] == int(round(even[2])) and fr["v1"] == int(round(odd[2]))
        w_even += fr["w"] == b.quant_w0(even[0])
        e_even += fr["e"] == b.quant_energy(even[3])
        n += 1
    return n, w, e, lsp_ok, v, w_even, e_even


def main():
    print(f"{'file':<28} {'n':>5} {'w0':>5} {'E':>5} {'lsp':>5} {'voice':>6}  even-w0 even-E")
    tot = np.zeros(6)
    for path in files():
        pcm = np.fromfile(path, dtype="<i2")
        frames, pitch, lsp = b.analyse(pcm)
        n, w, e, lsp_ok, v, w_even, e_even = tally(frames, pitch, lsp, 0)
        print(f"{os.path.basename(path):<28} {n:5d} {w:5d} {e:5d} {lsp_ok:5d} {v:6d}  {w_even:5d}  {e_even:5d}")
        tot += (n, w, e, lsp_ok, v, 0)
    print(f"{'ALL frames':<28} {int(tot[0]):5d} {int(tot[1]):5d} {int(tot[2]):5d} {int(tot[3]):5d} {int(tot[4]):6d}")
    tot2 = np.zeros(5)
    for path in files():
        pcm = np.fromfile(path, dtype="<i2")
        frames, pitch, lsp = b.analyse(pcm)
        n, w, e, lsp_ok, v, _we, _ee = tally(frames, pitch, lsp, 2)
        tot2 += (n, w, e, lsp_ok, v)
    print(f"{'from frame 2':<28} {int(tot2[0]):5d} {int(tot2[1]):5d} {int(tot2[2]):5d} {int(tot2[3]):5d} {int(tot2[4]):6d}")


if __name__ == "__main__":
    main()

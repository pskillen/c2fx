#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) The c2fx Authors
"""x01: the file header, the frame size, and which bits carry which field.

Settles: c2enc writes a 7-byte header only when the output name ends in .c2; c2dec accepts both;
the frame is 8 bytes; bit 0 is the MSB of byte 0; the field order 2+7+5+50 (found by flipping bits of
a frame and seeing what changes in the decoded speech).
"""
import numpy as np
import c2bb as b


def main():
    pcm = b.voiced_pulses(150, 0.5)
    raw, with_hdr, without = b.tmp(".raw"), b.tmp(".c2"), b.tmp(".bin")
    pcm.tofile(raw)
    modes = {}
    for mode in ("3200", "2400", "1600", "700C"):
        o = b.tmp(".c2")
        b.tool("c2enc", mode, raw, o)
        modes[mode] = open(o, "rb").read()[:7].hex()
    b.tool("c2enc", "3200", raw, with_hdr)
    b.tool("c2enc", "3200", raw, without)
    h, n = open(with_hdr, "rb").read(), open(without, "rb").read()
    print("headers by mode:", modes)
    print(f"3200 .c2 size {len(h)} = 7 + {(len(h) - 7)} ({(len(h) - 7) / 8} frames); .bin size {len(n)} ({len(n) / 8} frames)")
    print("frames identical apart from header:", h[7:] == n)
    out1, out2 = b.tmp(".raw"), b.tmp(".raw")
    b.tool("c2dec", "3200", with_hdr, out1)
    b.tool("c2dec", "3200", without, out2)
    a1, a2 = np.fromfile(out1, "<i2"), np.fromfile(out2, "<i2")
    print("c2dec output identical for headered and headerless input:", np.array_equal(a1, a2), len(a1), "samples")

    # which bits matter: flip each bit of a voiced frame, decode 60 copies, compare to the unflipped decode
    fr = b.encode(b.voiced_pulses(150, 1.0))[20]
    base = b.decode([fr] * 60)
    print("bit : change vs unflipped (rms of difference / rms of output), pitch (Hz) of flipped decode")
    for i in range(64):
        v = b.bits(fr) ^ (1 << (63 - i))
        o = b.decode([b.frame(v)] * 60)
        seg = o[3000:4600]
        d = b.rms(o[3000:4600].astype(float) - base[3000:4600]) / b.rms(base[3000:4600])
        print(f"{i:2d}: {d:5.2f}  {b.f0_of(seg, 40, 500):6.1f}")


if __name__ == "__main__":
    main()

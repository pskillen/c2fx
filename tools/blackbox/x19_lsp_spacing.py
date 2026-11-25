#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 The c2fx Authors
"""x19: why a frame with closely clustered LSPs decodes louder in the c2fx float model than in c2dec.

Frames have w0, energy and the seven upper LSP differences fixed; LSPs 1..3 sit at 175 Hz and then at spacings s = 25 Hz
(the tightest the tables allow) up to 800 Hz (LSP 2 = 175 + s, LSP 3 = 175 + 2s).

A. Both decoders (c2dec and build/c2m_dec) on steady frames: output rms and the strongest harmonic, per spacing.
   The gap is zero for wide spacing and grows as the LSPs close up, so something limits sharp envelope peaks.
B. Candidate rules, scored against c2dec's harmonic levels on tight frames (spacing 25..100 Hz, eight pitches), with
   speech-like frames (x09's) as the control. All use the envelope chain of x09 (recommended pair, offset 27.63 dB):
   bandwidth expansion of the LPC, a minimum LSP spacing, a cap on the spectrum peak, and a floor added to the
   power response |A|^2 before it is inverted (1/(|A|^2 + eps)).
C. What the winning rule predicts for the level of the vector files that disagree.
D. A separate effect: at very high energy indices c2dec's output falls instead of saturating.
"""
import os
import subprocess
import numpy as np
import c2bb as b
import x09_post_filter_fit as x9

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
MODEL = os.path.join(ROOT, "build", "c2m_dec")
GAMMA, BETA, OFFSET_E20 = 0.6, 0.275, 27.63     # x09's recommended set and its offset at e = 20
STEP = 50.0 / 32.0
UPPER = [4, 13, 18, 28, 26, 20, 9]               # LSP indices 4..10, as in sine_200hz frame 40
NF = 512


def model_decode(frames):
    if not os.path.exists(MODEL):
        subprocess.run(["make", "-C", ROOT, "build/c2m_dec"], check=True, capture_output=True)
    i, o = b.tmp(".bin"), b.tmp(".raw")
    open(i, "wb").write(b"".join(frames))
    subprocess.run([MODEL, i, o], check=True, capture_output=True)
    a = np.fromfile(o, "<i2")
    os.remove(i)
    os.remove(o)
    return a


def frame_idx(dk):
    return [6, dk, dk] + UPPER


def harmonics(pcm, f0, L):
    return np.array([20 * np.log10(b.harm_amp(pcm, k * f0) + 1e-9) for k in range(1, L + 1)])


def predict(lsp_hz, f0, L, e, eps=0.0, bw=1.0, minsep=0.0, cap=None):
    """Harmonic levels (dB) of the x09 chain at energy index e, with one candidate rule applied."""
    f = np.array(lsp_hz, float)
    if minsep:
        for i in range(1, 10):
            f[i] = max(f[i], f[i - 1] + minsep)
    a = b.lsp_to_lpc(f) * bw ** np.arange(11)
    fr = np.arange(NF // 2) * b.FS / NF
    A1 = np.abs(b.aresp(a, fr)) ** 2
    A2 = np.abs(b.aresp(a, fr, GAMMA)) ** 2
    P0 = 1.0 / (A1 + eps)
    if cap:
        P0 = np.minimum(P0, cap * P0.mean())
    P = P0 * ((A2 + eps) / (A1 + eps)) ** BETA
    P = P * (P0.sum() + 1e-4) / (P.sum() + 1e-4)
    P[:64] *= 1.96
    r = f0 * NF / b.FS
    out = []
    for m in range(1, L + 1):
        lo = int(np.floor((m - 0.5) * r + 0.5))
        hi = min(int(np.floor((m + 0.5) * r + 0.5)), 256)
        out.append(P[lo:hi].sum() if hi > lo else 1e-12)
    pw = np.array(out)
    if f0 < 150:
        pw[0] *= 0.032 ** 2
    return 10 * np.log10(pw) + OFFSET_E20 + STEP * (e - 20)


def part_a():
    print("A. spacing sweep: w0 index 100 (323 Hz), energy index 4, LSP1 = 175 Hz, LSP2 = 175+s, LSP3 = 175+2s")
    print(f"{'s (Hz)':>7} {'c2dec rms':>10} {'model rms':>10} {'gap dB':>7} {'c2dec peak dB':>14} {'model peak dB':>14}")
    f0 = b.w0_hz(100)
    L = int(4000 // f0)
    for dk in (0, 1, 2, 3, 4, 6, 8, 12, 16, 24, 31):
        fr = b.make_frame(1, 1, 100, 4, frame_idx(dk))
        a = b.decode([fr] * 30)[2400:4000]
        m = model_decode([fr] * 30)[2400:4000]
        print(f"{25 * (dk + 1):7d} {b.rms(a):10.0f} {b.rms(m):10.0f} {20 * np.log10(b.rms(m) / b.rms(a)):7.2f} "
              f"{harmonics(a, f0, L).max():14.1f} {harmonics(m, f0, L).max():14.1f}")


def tight_data():
    out = []
    for dk in (0, 1, 2, 3):
        for wi in (10, 14, 20, 26, 34, 40, 50, 64):
            idx = frame_idx(dk)
            f0 = b.w0_hz(wi)
            L = int(4000 // f0)
            a = b.decode([b.make_frame(1, 1, wi, 4, idx)] * 40)[3000:5400]
            out.append((b.lsp_hz_from_indices(idx), f0, L, harmonics(a, f0, L)))
    return out


def score(data, e, window, **kw):
    errs = []
    for lsp, f0, L, meas in data:
        pr = predict(lsp, f0, L, e, **kw)
        sel = (pr >= pr.max() - window) & (meas >= meas.max() - window)
        errs.append((meas - pr)[sel])
    err = np.concatenate(errs)
    return b.rms(err), float(err.mean())


def part_b(tight, speech):
    print("\nB. rules scored against c2dec: rms error (dB) of harmonic level over harmonics within 25 dB of the strongest")
    print("   (tight frames: %d frames, spacing 25..100 Hz) and over speech-like frames (x09's, within 40 dB)" % len(tight))

    def both(**kw):
        t = score(tight, 4, 25.0, **kw)
        s = score([(f, f0, L, m) for f, f0, L, m in speech], 20, 40.0, **kw)
        return "tight %.2f (mean %+.2f)   speech %.2f" % (t[0], t[1], s[0])

    print(f"  {'no rule':<34} {both()}")
    for bw in (0.99, 0.985, 0.98):
        print(f"  {'bandwidth expansion ' + str(bw):<34} {both(bw=bw)}")
    for ms in (40, 50, 60):
        print(f"  {'minimum LSP spacing ' + str(ms) + ' Hz':<34} {both(minsep=ms)}")
    for cap in (10, 30):
        print(f"  {'cap peak at ' + str(cap) + 'x the mean':<34} {both(cap=cap)}")
    best = None
    print("  floor eps added to |A|^2 before inversion:")
    for eps in (2e-7, 4e-7, 6e-7, 8e-7, 1e-6, 1.5e-6, 2e-6, 3e-6, 5e-6, 1e-5):
        r = both(eps=eps)
        t = score(tight, 4, 25.0, eps=eps)[0]
        if best is None or t < best[0]:
            best = (t, eps)
        print(f"    eps {eps:<8g} {r}")
    print("  best eps on the tight frames: %g (%.2f dB)" % (best[1], best[0]))
    return best[1]


def part_c(eps):
    print("\nC. predicted level change of the floor eps = %g on the vector files whose decodes disagree" % eps)
    print("   measured gap = model rms over c2dec rms; predicted drop = power-weighted change of total harmonic power")
    print("   over the voiced frames when the floor is applied (the file rms is dominated by its loud frames)")
    for name in ("sine_200hz", "sine_100hz", "chirp_80_400hz", "sine_200hz_clipped", "high_voice"):
        raw = open(os.path.join(ROOT, "vectors", name + ".c2"), "rb").read()
        frs = [raw[k:k + 8] for k in range(0, len(raw) - 7, 8)]
        a, m = b.decode(frs), model_decode(frs)
        n = min(len(a), len(m))
        gap = 20 * np.log10(b.rms(m[:n]) / b.rms(a[:n]))
        w0 = w1 = 0.0
        for fr in frs[2:]:
            u = b.unpack(fr)
            if not (u["v0"] and u["v1"]):
                continue
            lsp = b.lsp_hz_from_indices(u["lsp"])
            f0 = b.w0_hz(u["w"])
            L = int(4000 // f0)
            w0 += np.sum(10 ** (predict(lsp, f0, L, u["e"]) / 10))
            w1 += np.sum(10 ** (predict(lsp, f0, L, u["e"], eps=eps) / 10))
        drop = 10 * np.log10(w0 / w1)
        print(f"  {name:<20} measured gap {gap:6.2f} dB; predicted drop {drop:6.2f} dB; remaining {gap - drop:+6.2f} dB")


def part_d():
    print("\nD. overload: steady frame of sine_200hz_clipped (frame 40: LSP indices 7 2 13 3 18 6 21 15 14 20, w0 index 94)")
    print("   with only the energy index changed. c2dec follows the chain (less 0.2-0.4 dB) up to e = 27 and then")
    print("   its output FALLS by 1.5625 dB per step; the float model saturates at 32767 instead.")
    idx = [7, 2, 13, 3, 18, 6, 21, 15, 14, 20]
    f0 = b.w0_hz(94)
    L = int(4000 // f0)
    base = b.make_frame(1, 1, 94, 24, idx)
    m0 = model_decode([base] * 40).astype(float)[3000:5400]
    a0 = b.decode([base] * 40).astype(float)[3000:5400]
    gain = np.sum(a0 * m0) / np.sum(m0 * m0)     # c2dec over model at e = 24 (no overload)
    print(f"{'e':>3} {'c2dec max':>10} {'c2dec rms':>10} {'model max':>10} {'model rms':>10} {'c2dec h1 dB':>12} {'chain h1 dB':>12}"
          f" {'wrap rms':>9} {'sat rms':>8}")
    for e in (24, 26, 27, 28, 29, 30, 31):
        fr = b.make_frame(1, 1, 94, e, idx)
        a = b.decode([fr] * 40)[3000:5400]
        m = model_decode([fr] * 40)[3000:5400]
        h = harmonics(a, f0, L)[0]
        pr = predict(b.lsp_hz_from_indices(idx), f0, L, e)[0]
        un = np.round(m0 * gain * 10 ** (STEP * (e - 24) / 20))   # the level the signal would have without a limit
        wrap = ((un + 32768) % 65536) - 32768
        sat = np.clip(un, -32768, 32767)
        print(f"{e:3d} {int(np.abs(a).max()):10d} {b.rms(a):10.0f} {int(np.abs(m).max()):10d} {b.rms(m):10.0f} {h:12.1f} {pr:12.1f}"
              f" {b.rms(wrap):9.0f} {b.rms(sat):8.0f}")


def main():
    part_a()
    tight = tight_data()
    speech = x9.collect()
    part_b(tight, speech)
    part_c(1e-6)
    part_d()


if __name__ == "__main__":
    main()

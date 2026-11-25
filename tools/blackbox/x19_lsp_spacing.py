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
EPS = 1e-6                                       # the floor the spec recommends
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


def predict(lsp_hz, f0, L, e, eps=0.0, bw=1.0, minsep=0.0, cap=None, ratio="both"):
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
    if ratio == "both":        # floor in numerator and denominator of the post-filter ratio
        rr = (A2 + eps) / (A1 + eps)
    elif ratio == "denom":      # floor in the denominator only
        rr = A2 / (A1 + eps)
    else:                       # no floor in the ratio
        rr = A2 / A1
    P = P0 * rr ** BETA
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
    print(f"{'s (Hz)':>7} {'c2dec rms':>10} {'model rms':>10} {'gap dB':>7} {'c2dec peak dB':>14} {'model peak dB':>14} {'min |A|^2':>10}")
    f0 = b.w0_hz(100)
    L = int(4000 // f0)
    for dk in (0, 1, 2, 3, 4, 6, 8, 12, 16, 24, 31):
        fr = b.make_frame(1, 1, 100, 4, frame_idx(dk))
        a = b.decode([fr] * 30)[2400:4000]
        m = model_decode([fr] * 30)[2400:4000]
        amin = (np.abs(b.aresp(b.lsp_to_lpc(b.lsp_hz_from_indices(frame_idx(dk))), np.arange(256) * b.FS / NF)) ** 2).min()
        print(f"{25 * (dk + 1):7d} {b.rms(a):10.0f} {b.rms(m):10.0f} {20 * np.log10(b.rms(m) / b.rms(a)):7.2f} "
              f"{harmonics(a, f0, L).max():14.1f} {harmonics(m, f0, L).max():14.1f} {amin:10.2g}")


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
    for eps in (2e-7, 4e-7, 6e-7, 8e-7, 1e-6, 1.25e-6, 1.5e-6, 2e-6, 3e-6, 5e-6, 1e-5):
        r = both(eps=eps)
        t = score(tight, 4, 25.0, eps=eps)[0]
        if best is None or t < best[0]:
            best = (t, eps)
        print(f"    eps {eps:<8g} {r}")
    print("  best eps on the tight frames: %g (%.2f dB)" % (best[1], best[0]))
    print("  floor in the post-filter ratio |A(z/gamma)|^2 / |A(z)|^2 (tight rms, speech rms), by eps:")
    print(f"    {'eps':<8} {'numerator and denominator':>26} {'denominator only':>18} {'neither':>10}")
    for eps in (1e-6, 1.25e-6, 1.5e-6):
        cells = []
        for variant in ("both", "denom", "none"):
            t = score(tight, 4, 25.0, eps=eps, ratio=variant)[0]
            sp = score([(f, f0, L, m) for f, f0, L, m in speech], 20, 40.0, eps=eps, ratio=variant)[0]
            cells.append("%.3f, %.2f" % (t, sp))
        print(f"    {eps:<8g} {cells[0]:>26} {cells[1]:>18} {cells[2]:>10}")
    return best[1]


CLUSTERS = [
    ("LSPs 2-4 near 200 Hz", [120, 200, 225, 250, 900, 1500, 2100, 2700, 3200, 3600], 40),
    ("LSPs 5-7 near 1.1 kHz", [250, 500, 750, 950, 1100, 1125, 1150, 2000, 2600, 3300], 20),
    ("LSPs 8-10 near 2.1 kHz", [300, 600, 900, 1200, 1500, 1800, 2000, 2100, 2125, 2150], 100),
]


def part_b2():
    print("\nB2. other cluster positions, rms error (dB) against c2dec over harmonics within 25 dB of the strongest;")
    print("    energy index 4, LSPs quantized closed-loop from the frequencies shown")
    print(f"    {'cluster':<26} {'w0 idx':>6} {'no floor':>9} {'1e-6':>7} {'1.5e-6':>8}")
    for name, hz, wi in CLUSTERS:
        idx = b.quant_lsp(np.array(hz) * 2 * np.pi / b.FS)
        f0 = b.w0_hz(wi)
        L = int(4000 // f0)
        a = b.decode([b.make_frame(1, 1, wi, 4, idx)] * 40)[3000:5400]
        data = [(b.lsp_hz_from_indices(idx), f0, L, harmonics(a, f0, L))]
        r = [score(data, 4, 25.0, eps=e)[0] for e in (0.0, 1e-6, 1.5e-6)]
        print(f"    {name:<26} {wi:6d} {r[0]:9.2f} {r[1]:7.2f} {r[2]:8.2f}")


def part_c(epss):
    print("\nC. predicted level change of the floor on the vector files whose decodes disagree")
    print("   measured gap = model rms over c2dec rms; predicted drop = power-weighted change of total harmonic power")
    print("   over the voiced frames when the floor is applied (the file rms is dominated by its loud frames)")
    for name in ("sine_200hz", "sine_100hz", "chirp_80_400hz", "sine_200hz_clipped", "high_voice"):
        raw = open(os.path.join(ROOT, "vectors", name + ".c2"), "rb").read()
        frs = [raw[k:k + 8] for k in range(0, len(raw) - 7, 8)]
        a, m = b.decode(frs), model_decode(frs)
        n = min(len(a), len(m))
        gap = 20 * np.log10(b.rms(m[:n]) / b.rms(a[:n]))
        w0 = 0.0
        w1 = [0.0] * len(epss)
        for fr in frs[2:]:
            u = b.unpack(fr)
            if not (u["v0"] and u["v1"]):
                continue
            lsp = b.lsp_hz_from_indices(u["lsp"])
            f0 = b.w0_hz(u["w"])
            L = int(4000 // f0)
            w0 += np.sum(10 ** (predict(lsp, f0, L, u["e"]) / 10))
            for i, eps in enumerate(epss):
                w1[i] += np.sum(10 ** (predict(lsp, f0, L, u["e"], eps=eps) / 10))
        cells = "; ".join(f"eps {eps:g}: drop {10 * np.log10(w0 / w1[i]):5.2f}, remaining {gap - 10 * np.log10(w0 / w1[i]):+5.2f}"
                          for i, eps in enumerate(epss))
        print(f"  {name:<20} measured gap {gap:6.2f} dB; {cells}")


def overload_rows(label, idx, wi, energies, base_e):
    f0 = b.w0_hz(wi)
    L = int(4000 // f0)
    base = b.make_frame(1, 1, wi, base_e, idx)
    m0 = model_decode([base] * 40).astype(float)[3000:5400]
    a0 = b.decode([base] * 40).astype(float)[3000:5400]
    gain = np.sum(a0 * m0) / np.sum(m0 * m0)     # c2dec over model at the base energy (no overload there)
    print(f"  {label}: LSP indices {idx}, w0 index {wi}")
    print(f"  {'e':>3} {'unlimited peak':>15} {'c2dec max':>10} {'c2dec rms':>10} {'model max':>10} {'model rms':>10} "
          f"{'h1-chain dB':>14} {'wrap rms':>9} {'sat rms':>8}")
    for e in energies:
        fr = b.make_frame(1, 1, wi, e, idx)
        a = b.decode([fr] * 40)[3000:5400]
        m = model_decode([fr] * 40)[3000:5400]
        h = harmonics(a, f0, L)[0]
        pr = predict(b.lsp_hz_from_indices(idx), f0, L, e, eps=EPS)[0]
        un = np.round(m0 * gain * 10 ** (STEP * (e - base_e) / 20))     # the level the signal would have without a limit
        wrap = ((un + 32768) % 65536) - 32768
        sat = np.clip(un, -32768, 32767)
        print(f"  {e:3d} {np.abs(un).max():15.0f} {int(np.abs(a).max()):10d} {b.rms(a):10.0f} {int(np.abs(m).max()):10d} {b.rms(m):10.0f} "
              f"{h - pr:14.1f} {b.rms(wrap):9.0f} {b.rms(sat):8.0f}")


def part_d():
    print("\nD. overload: steady frames with only the energy index changed. 'unlimited peak' is the largest sample the")
    print("   model's output would have without any limit (scaled from a quiet decode, times c2dec/model gain).")
    overload_rows("frame 40 of sine_200hz_clipped", [7, 2, 13, 3, 18, 6, 21, 15, 14, 20], 94, (24, 26, 27, 28, 29, 30, 31), 24)
    print()
    overload_rows("tight frame (LSPs 175, 200, 225 Hz)", frame_idx(0), 100, (10, 12, 13, 14, 15, 16, 17, 18), 8)
    print("\n   control: wide-spacing frames at the same energies show no fall (c2dec rms and model-minus-c2dec gap in dB)")
    for wi in (60, 100):
        idx = [10] * 10
        cells = []
        for e in (20, 24, 28, 31):
            fr = b.make_frame(1, 1, wi, e, idx)
            a = b.decode([fr] * 40)[3000:5400]
            m = model_decode([fr] * 40)[3000:5400]
            cells.append(f"e={e}: {b.rms(a):.0f} ({20 * np.log10(b.rms(m) / b.rms(a)):+.2f})")
        print(f"   w0 index {wi}, LSP indices {idx}: " + "; ".join(cells))


def main():
    part_a()
    tight = tight_data()
    speech = x9.collect()
    part_b(tight, speech)
    part_b2()
    part_c((1e-6, 1.25e-6, 1.5e-6))
    part_d()


if __name__ == "__main__":
    main()

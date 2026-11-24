#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 The c2fx Authors
"""x05: the post filter and bass boost. c2dec's --nopf has no effect on 3200; the envelope the decoder
applies is -20log|A| plus a post-filter term plus a low-frequency step.

Part A: --nopf changes nothing (bitwise).
Part B: grid of post-filter variants scored against measured harmonic levels over random frames:
Rw = |A(z/gamma)/A(z)|^beta in amplitude, an optional +3 dB step below 1 kHz, a power normalisation
(keep the sum of the 512-point power spectrum) before or after the step, harmonic amplitude as a band
sum or a point sample. Prints the best variants. The grid does NOT pin gamma/beta uniquely.
"""
import itertools
import numpy as np
import c2bb as b

NF = 512


def bins(a, gamma=1.0, nf=NF):
    return b.aresp(a, np.arange(nf // 2) * b.FS / nf, gamma)


def predict(f, f0, L, gamma, beta, bass, norm, samp, nf=NF):
    a = b.lsp_to_lpc(f)
    H2 = 1 / np.abs(bins(a, 1.0, nf)) ** 2
    P = H2 * (np.abs(bins(a, gamma, nf)) / np.abs(bins(a, 1.0, nf))) ** (2 * beta)
    k = np.arange(nf // 2)
    step = np.where(k * b.FS / nf < 1000, 10 ** 0.3, 1.0)
    if bass == "pre":
        P = P * step
    if norm:
        P = P * H2.sum() / P.sum()
    if bass == "post":
        P = P * step
    r = f0 * nf / b.FS
    out = []
    for m in range(1, L + 1):
        lo, hi = int(np.floor((m - 0.5) * r + 0.5)), int(np.floor((m + 0.5) * r + 0.5))
        hi = max(hi, lo + 1)
        out.append(P[lo:hi].sum() if samp == "band" else P[min(int(np.floor(m * r + 0.5)), nf // 2 - 1)] * r)
    return 10 * np.log10(np.array(out))


def main():
    fr = b.encode(b.voiced_pulses(120, 1.0))
    print("A. --nopf output identical to default:", np.array_equal(b.decode(fr), b.decode(fr, ("--nopf",))))

    rng = np.random.default_rng(21)
    data = []
    for wi in (10, 25, 40, 55, 70, 90):
        f0 = b.w0_hz(wi)
        L = int(4000 // f0)
        for _ in range(8):
            while True:
                g = rng.integers(3, 18, 10)
                f = b.lsp_hz_from_indices(g)
                if 2800 < f[-1] < 3800:
                    break
            seg = b.decode([b.make_frame(1, 1, wi, 20, g)] * 40)[3000:5400]
            amp = np.array([b.harm_amp(seg, m * f0) for m in range(1, L + 1)])
            data.append((f, f0, L, amp))

    def score(**kw):
        errs, offs = [], []   # offs: per-frame constant (dB) between measured and predicted
        for f, f0, L, amp in data:
            s = 1 if f0 < 150 else 0
            e = (20 * np.log10(amp + 1e-9) - predict(f, f0, L, **kw))[s:]
            offs.append(e.mean())
            errs.append(e - e.mean())
        allerr = np.concatenate(errs)
        return b.rms(allerr), float(np.mean(offs)), float(np.std(offs))

    res = []
    for gamma, beta, bass, norm, samp in itertools.product((0.4, 0.5, 0.6), (0.0, 0.1, 0.2, 0.25, 0.3), ("none", "pre", "post"), (False, True), ("band", "point")):
        s = score(gamma=gamma, beta=beta, bass=bass, norm=norm, samp=samp)
        res.append((s[0] + s[2], s, gamma, beta, bass, norm, samp))
    res.sort(key=lambda x: x[0])
    print("B. best variants: score = shape rms + per-frame offset sd (dB)")
    for r in res[:10]:
        print("  score %.2f | shape-rms %.2f const %.2f offset-sd %.2f | gamma %.1f beta %.2f bass %s norm %s samp %s" % ((r[0], r[1][0], r[1][1], r[1][2]) + r[2:]))
    print("  no post filter at all (beta 0, no bass, no norm, band):", score(gamma=0.5, beta=0.0, bass="none", norm=False, samp="band"))

    print("C. envelope DFT size: shape rms, mean offset, offset sd (dB) for gamma .5 beta .2 bass post norm, band sum")
    for nf in (128, 256, 512, 1024, 2048):
        print("  N_dft", nf, np.round(score(gamma=0.5, beta=0.2, bass="post", norm=True, samp="band", nf=nf), 2))


if __name__ == "__main__":
    main()

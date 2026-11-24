#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 The c2fx Authors
"""x17: the two voicing bits, and how a tone-plus-noise mixture is classified.

Bit 0 of codec frame k is the voicing flag of 10 ms row 2k, bit 1 the flag of
row 2k+1 (pitch-dump column 3, 0 or 1). The bits are those two flags, not a
function that merges them.

The mixture is an 180 Hz sine of amplitude 8000 plus gaussian noise. The sine's
mean square is 8000^2/2. SNR is 10*log10 of that over the noise mean square.
The voiced fraction is over 10 ms rows, excluding the first four and the last.
"""
import numpy as np
import c2bb as b


def mix(snr_db, n=8000, f=180, amp=8000, seed=7):
    t = np.arange(n)
    tone = amp * np.sin(2 * np.pi * f * t / b.FS)
    sig_power = amp * amp / 2
    noise_power = sig_power / 10 ** (snr_db / 10)
    rng = np.random.default_rng(seed)
    noise = rng.normal(0.0, np.sqrt(noise_power), n)
    return np.clip(np.round(tone + noise), -32767, 32767).astype(np.int16)


def main():
    print("bits against the two 10 ms flags")
    rng = np.random.default_rng(2)
    cases = {
        "pulses": b.voiced_pulses(130, 1.0, 8000),
        "noise": rng.integers(-8000, 8000, 8000).astype(np.int16),
        "tone": mix(40),
        "tone+noise 6 dB": mix(6),
        "tone+noise 0 dB": mix(0),
    }
    for name, pcm in cases.items():
        frames, pitch, _lsp = b.analyse(pcm)
        bad = 0
        n = 0
        for k in range(1, len(frames) - 1):
            v0 = int(round(pitch[2 * k][2]))
            v1 = int(round(pitch[2 * k + 1][2]))
            bad += frames[k]["v0"] != v0 or frames[k]["v1"] != v1
            n += 1
        print(f"  {name}: bit mismatches {bad}/{n}")

    print("\n180 Hz tone plus noise: fraction of 10 ms rows flagged voiced")
    print("SNR_dB   voiced_rows   both_bits_1   either_bit_1")
    for snr in (20, 12, 6, 3, 0, -3, -6):
        frames, pitch, _lsp = b.analyse(mix(snr))
        rows = pitch[4:-1]
        voiced = sum(int(round(r[2])) == 1 for r in rows)
        both = either = frames_n = 0
        for k in range(2, len(frames) - 1):
            both += frames[k]["v0"] == 1 and frames[k]["v1"] == 1
            either += frames[k]["v0"] == 1 or frames[k]["v1"] == 1
            frames_n += 1
        print(f"{snr:5d}  {voiced:5d}/{len(rows):<4d}  {both:5d}/{frames_n:<3d}  {either:5d}/{frames_n}")


if __name__ == "__main__":
    main()

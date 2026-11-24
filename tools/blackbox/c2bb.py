# SPDX-License-Identifier: MIT
# Copyright (c) The c2fx Authors
"""Shared helpers for the black-box experiments: run c2enc / c2dec / c2sim from the kit,
pack and unpack 3200 frames, make test signals. Only runs the kit's tools; never reads source.

Environment: C2FX_BLACKBOX (kit, default ~/build/c2-blackbox), C2FX_WORK (scratch, default ./work).
Needs numpy (see README.md)."""
import os
import subprocess
import tempfile
import numpy as np

BB = os.path.expanduser(os.environ.get("C2FX_BLACKBOX", "~/build/c2-blackbox"))
WORK = os.path.abspath(os.environ.get("C2FX_WORK", "work"))
os.makedirs(WORK, exist_ok=True)
FS = 8000
HEADER_LEN = 7


def tool(name, *args):
    r = subprocess.run([os.path.join(BB, "bin", name)] + [str(a) for a in args],
                       capture_output=True, text=True)
    if r.returncode != 0:
        raise RuntimeError(f"{name} {args}: {r.stderr}")
    return r


def tmp(suffix):
    fd, p = tempfile.mkstemp(suffix=suffix, dir=WORK)
    os.close(fd)
    return p


def encode(pcm, extra=()):
    """int16 array -> list of 8-byte frames (headerless output)."""
    i, o = tmp(".raw"), tmp(".bin")
    np.asarray(pcm, dtype="<i2").tofile(i)
    tool("c2enc", "3200", i, o, *extra)
    d = open(o, "rb").read()
    os.remove(i)
    os.remove(o)
    return [d[k:k + 8] for k in range(0, len(d) - 7, 8)]


def decode(frames, extra=()):
    """list of 8-byte frames -> int16 array (headerless input)."""
    i, o = tmp(".bin"), tmp(".raw")
    open(i, "wb").write(b"".join(frames))
    tool("c2dec", "3200", i, o, *extra)
    a = np.fromfile(o, dtype="<i2")
    os.remove(i)
    os.remove(o)
    return a


def bits(frame):
    """64-bit int, first transmitted bit = MSB of byte 0 (bit 63)."""
    return int.from_bytes(frame, "big")


def frame(v):
    return int(v).to_bytes(8, "big")


def field(frame_, first, width):
    """Bits [first, first+width) counted from the MSB of byte 0."""
    return (bits(frame_) >> (64 - first - width)) & ((1 << width) - 1)


def put(v, first, width, value):
    sh = 64 - first - width
    v &= ~(((1 << width) - 1) << sh)
    return v | (value << sh)


def voiced_pulses(f0, secs=1.0, amp=8000, formants=((700, 100), (1200, 120), (2600, 200))):
    """Pulse train through a few resonators: a simple voiced vowel-like signal."""
    n = int(FS * secs)
    x = np.zeros(n)
    t = 0.0
    while t < n:
        x[int(t)] = 1.0
        t += FS / f0
    y = x
    for fc, bw in formants:
        r = np.exp(-np.pi * bw / FS)
        th = 2 * np.pi * fc / FS
        a1, a2 = -2 * r * np.cos(th), r * r
        z = np.zeros(n)
        for k in range(n):
            z[k] = y[k] - a1 * (z[k - 1] if k > 0 else 0) - a2 * (z[k - 2] if k > 1 else 0)
        y = z
    y = y / np.max(np.abs(y)) * amp
    return y.astype(np.int16)


def f0_of(pcm, lo=50, hi=450):
    """Autocorrelation pitch estimate (Hz) of a decoded segment; 0 if silent."""
    x = np.asarray(pcm, float)
    x = x - x.mean()
    if np.sum(x * x) < 1e-3:
        return 0.0
    ac = np.correlate(x, x, "full")[len(x) - 1:]
    a, b = int(FS / hi), int(FS / lo)
    k = a + int(np.argmax(ac[a:b]))
    return FS / k


# ---------------------------------------------------------------------------
# Analysis helpers used by several experiments (textbook formulas, not codec code)

def gray_enc(n):
    return n ^ (n >> 1)


def gray_dec(g):
    n = 0
    while g:
        n ^= g
        g >>= 1
    return n


def codebook(name):
    """Read a kit codebook file as a list of floats: first line is 'dims entries', then the numbers."""
    t = open(os.path.join(BB, "codebook", name + ".txt")).read().split()
    return [float(x) for x in t[2:]]


DLSP = [codebook(f"dlsp{i}") for i in range(1, 11)]


def lsp_hz_from_indices(idx):
    """Cumulative sum of the ten delta-table entries (Hz). idx = ten decoded (non-Gray) 5-bit indices."""
    return np.cumsum([DLSP[j][i] for j, i in enumerate(idx)])


def lsp_to_lpc(f_hz):
    """Textbook LSP -> LPC (order 10): A(z) = (P(z)+Q(z))/2. Returns a[0..10], a[0] = 1."""
    w = 2 * np.pi * np.asarray(f_hz) / FS
    p = np.array([1.0, 1.0])
    q = np.array([1.0, -1.0])
    for i in range(0, 10, 2):
        p = np.convolve(p, [1, -2 * np.cos(w[i]), 1])
        q = np.convolve(q, [1, -2 * np.cos(w[i + 1]), 1])
    return ((p + q) / 2)[:11]


def aresp(a, f_hz, gamma=1.0):
    """A(z/gamma) on the unit circle at the given frequencies (Hz)."""
    w = 2 * np.pi * np.asarray(f_hz) / FS
    return sum(a[k] * gamma ** k * np.exp(-1j * k * w) for k in range(len(a)))


def make_frame(v1, v2, w0_idx, e_idx, lsp_idx):
    """Pack a 3200 frame from decoded (non-Gray) field values: 2 voicing bits, 7-bit w0 index,
    5-bit energy index, ten 5-bit LSP delta indices; every multi-bit field Gray coded."""
    v = 0
    v = put(v, 0, 1, v1)
    v = put(v, 1, 1, v2)
    v = put(v, 2, 7, gray_enc(w0_idx))
    v = put(v, 9, 5, gray_enc(e_idx))
    for k, x in enumerate(lsp_idx):
        v = put(v, 14 + 5 * k, 5, gray_enc(int(x)))
    return frame(v)


def w0_hz(i):
    """Fundamental (Hz) for 7-bit index i, as measured in x02."""
    return 50 + i * 350 / 128


def harm_amp(pcm, f_hz):
    """Amplitude of the sinusoid at f_hz in a stationary segment (complex demodulation, mean)."""
    t = np.arange(len(pcm))
    return 2 * np.abs((np.asarray(pcm, float) * np.exp(-2j * np.pi * f_hz * t / FS)).mean())


def harm_env(pcm, f_hz, win=40):
    """Amplitude envelope of the sinusoid at f_hz: demodulate, boxcar of win samples."""
    t = np.arange(len(pcm))
    z = np.asarray(pcm, float) * np.exp(-2j * np.pi * f_hz * t / FS)
    return 2 * np.abs(np.convolve(z, np.ones(win) / win, "same"))


def rms(x):
    x = np.asarray(x, float)
    return float(np.sqrt(np.mean(x * x)))

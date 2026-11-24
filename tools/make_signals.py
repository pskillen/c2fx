#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 The c2fx Authors

"""Write tests/data/signals, 16-bit signed little-endian PCM, 8 kHz, mono, no header.

Every file is 2.000 s (16000 samples, 100 frames of 160). The same run writes
the same bytes. Tones and the chirp use amplitude 16000, which stays inside
the int16 range. The clipped tone is the exception.

Files:
  sine_100hz.pcm, sine_150hz.pcm, sine_200hz.pcm, sine_300hz.pcm, sine_400hz.pcm
      sample[i] = round(16000 * sin(2 * pi * f * i / 8000))
  chirp_80_400hz.pcm
      linear chirp, 80 Hz at t = 0 to 400 Hz at t = 2 s, amplitude 16000
      phase(t) = 2 * pi * (f0 * t + (f1 - f0) * t * t / (2 * T))
  noise_xorshift.pcm
      Marsaglia xorshift32 (shifts 13, 17, 5), seed 0xC2F32001
      each sample is the high 16 bits of the state, as a signed int16
  silence.pcm
      16000 zeros
  dc_8000.pcm
      16000 samples of +8000
  sine_200hz_clipped.pcm
      200 Hz sine of amplitude 60000, then clamped to +/-32767
"""

import math
import struct
from pathlib import Path

SAMPLE_RATE = 8000
SAMPLES = 16000
FRAME = 160
AMPLITUDE = 16000
CLIP_AMPLITUDE = 60000
CLIP = 32767
XORSHIFT_SEED = 0xC2F32001

OUT_DIR = Path(__file__).resolve().parent.parent / "tests" / "data" / "signals"


def quantize(value: float) -> int:
    sample = int(round(value))
    if sample > CLIP:
        return CLIP
    if sample < -CLIP:
        return -CLIP
    return sample


def sine(frequency: float, amplitude: float) -> list[int]:
    samples = []
    for index in range(SAMPLES):
        value = amplitude * math.sin(2 * math.pi * frequency * index / SAMPLE_RATE)
        samples.append(quantize(value))
    return samples


def chirp() -> list[int]:
    duration = SAMPLES / SAMPLE_RATE
    samples = []
    for index in range(SAMPLES):
        t = index / SAMPLE_RATE
        phase = 2 * math.pi * (80 * t + (400 - 80) * t * t / (2 * duration))
        samples.append(quantize(AMPLITUDE * math.sin(phase)))
    return samples


def xorshift32(state: int) -> int:
    state &= 0xFFFFFFFF
    state ^= (state << 13) & 0xFFFFFFFF
    state ^= (state >> 17) & 0xFFFFFFFF
    state ^= (state << 5) & 0xFFFFFFFF
    return state & 0xFFFFFFFF


def noise() -> list[int]:
    state = XORSHIFT_SEED
    samples = []
    for _ in range(SAMPLES):
        state = xorshift32(state)
        word = (state >> 16) & 0xFFFF
        if word >= 0x8000:
            word -= 0x10000
        samples.append(word)
    return samples


def pack(samples: list[int]) -> bytes:
    if len(samples) != SAMPLES or SAMPLES % FRAME != 0:
        raise SystemExit("signal is not a whole number of 2 s frames")
    for sample in samples:
        if sample < -32768 or sample > 32767:
            raise SystemExit("sample does not fit in int16: %d" % sample)
    return struct.pack("<%dh" % len(samples), *samples)


def main() -> None:
    signals = {
        "sine_100hz.pcm": sine(100, AMPLITUDE),
        "sine_150hz.pcm": sine(150, AMPLITUDE),
        "sine_200hz.pcm": sine(200, AMPLITUDE),
        "sine_300hz.pcm": sine(300, AMPLITUDE),
        "sine_400hz.pcm": sine(400, AMPLITUDE),
        "chirp_80_400hz.pcm": chirp(),
        "noise_xorshift.pcm": noise(),
        "silence.pcm": [0] * SAMPLES,
        "dc_8000.pcm": [8000] * SAMPLES,
        "sine_200hz_clipped.pcm": sine(200, CLIP_AMPLITUDE),
    }
    clipped = signals["sine_200hz_clipped.pcm"]
    if CLIP not in clipped and -CLIP not in clipped:
        raise SystemExit("clipped sine never reached +/-32767")
    tone = signals["sine_200hz.pcm"]
    if max(abs(sample) for sample in tone) >= CLIP:
        raise SystemExit("plain 200 Hz sine clipped")
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    for name, samples in signals.items():
        (OUT_DIR / name).write_bytes(pack(samples))
        print(name)


if __name__ == "__main__":
    main()

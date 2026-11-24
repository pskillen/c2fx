#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 The c2fx Authors

"""Wrap raw 8 kHz mono int16 PCM as a WAV file. No dependencies beyond Python."""

import sys
import wave

def main() -> int:
    if len(sys.argv) != 3:
        print("usage: pcm_to_wav.py <in.pcm> <out.wav>", file=sys.stderr)
        return 2
    with open(sys.argv[1], "rb") as raw:
        pcm = raw.read()
    if len(pcm) % 2 != 0:
        print("pcm length is not a whole number of samples", file=sys.stderr)
        return 1
    with wave.open(sys.argv[2], "wb") as out:
        out.setnchannels(1)
        out.setsampwidth(2)
        out.setframerate(8000)
        out.writeframes(pcm)
    return 0

if __name__ == "__main__":
    raise SystemExit(main())

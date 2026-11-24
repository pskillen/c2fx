#!/bin/bash
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 The c2fx Authors

# Rebuild tests/data/voice/low_voice.pcm and high_voice.pcm.
#
# Versions used for the committed files:
#   espeak-ng 1.52.0
#   SoX_ng 14.7.0.9 (invoked as sox)
#
# Both phrases speak the same text. No callsigns or names:
#   This is a test transmission. One two three four five. The signal is clear and steady.
#
#   espeak-ng -v en-us+m3 -p 15 -s 155 -D -w low_voice.wav "<text>"
#   espeak-ng -v en-us+f3 -p 85 -s 155 -D -w high_voice.wav "<text>"
#   sox -D <wav> -t raw -r 8000 -c 1 -e signed-integer -b 16 <raw>
#   sox -D -t raw -r 8000 -e signed-integer -b 16 -c 1 <raw> \
#       -t raw <trimmed> trim 0 <N>s
#   sox -D -t raw -r 8000 -e signed-integer -b 16 -c 1 <trimmed> \
#       -t raw <out.pcm> vol <gain>
#
# <N> is the sample count rounded down to a multiple of 160 (one 20 ms frame).
# <gain> is the RMS of test_transmission.pcm divided by the RMS of the trimmed
# phrase, printed to 12 digits. espeak-ng -D is its deterministic mode. sox -D
# turns dither off so the resample and the gain round the same way every run.
#
# The two files copied from RadioFoundry are not regenerated here.

set -euo pipefail
cd "$(dirname "$0")/.."

VOICE=tests/data/voice
REF="$VOICE/test_transmission.pcm"
TEXT='This is a test transmission. One two three four five. The signal is clear and steady.'

if [[ ! -f "$REF" ]]; then
  echo "missing $REF" >&2
  exit 1
fi

echo "espeak-ng $(espeak-ng --version | head -n 1)"
echo "sox $(sox --version)"

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

rms() {
  python3 -c '
import math, struct, sys
data = open(sys.argv[1], "rb").read()
count = len(data) // 2
if count == 0 or len(data) != count * 2:
    raise SystemExit("bad pcm: " + sys.argv[1])
samples = struct.unpack("<%dh" % count, data)
print("%.12f" % math.sqrt(sum(sample * sample for sample in samples) / count))
' "$1"
}

render() {
  local name=$1 voice=$2 pitch=$3
  local bytes samples keep cur ref factor out_rms

  espeak-ng -v "$voice" -p "$pitch" -s 155 -D -w "$tmp/$name.wav" "$TEXT"
  sox -D "$tmp/$name.wav" -t raw -r 8000 -c 1 -e signed-integer -b 16 "$tmp/$name.s16"

  bytes=$(wc -c < "$tmp/$name.s16")
  samples=$(( bytes / 2 ))
  keep=$(( samples / 160 * 160 ))
  if (( keep < 8000 * 4 || keep > 8000 * 8 )); then
    echo "$name duration out of 4-8 s: $keep samples" >&2
    exit 1
  fi

  sox -D -t raw -r 8000 -e signed-integer -b 16 -c 1 "$tmp/$name.s16" \
    -t raw "$tmp/$name.trim" trim 0 "${keep}s"

  cur=$(rms "$tmp/$name.trim")
  ref=$(rms "$REF")
  factor=$(python3 -c 'import sys; print(format(float(sys.argv[1]) / float(sys.argv[2]), ".12f"))' "$ref" "$cur")
  sox -D -t raw -r 8000 -e signed-integer -b 16 -c 1 "$tmp/$name.trim" \
    -t raw "$VOICE/$name.pcm" vol "$factor"

  out_rms=$(rms "$VOICE/$name.pcm")
  python3 -c '
import math, sys
out_rms = float(sys.argv[1])
ref = float(sys.argv[2])
delta = abs(20 * math.log10(out_rms / ref))
print("%s RMS delta %0.3f dB" % (sys.argv[3], delta))
if delta > 3.0:
    raise SystemExit(1)
' "$out_rms" "$ref" "$name"
  echo "$name: $keep samples, gain $factor"
}

render low_voice en-us+m3 15
render high_voice en-us+f3 85

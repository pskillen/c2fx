#!/bin/bash
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 The c2fx Authors

# Build vectors/ from every .pcm under tests/data/voice and tests/data/signals.
#
# Kit: directory named by $C2FX_BLACKBOX (required).
# The kit is upstream Codec 2, run as a black box. This script does not
# build it, and CI must not either.
#
# For each <set>.pcm (whole 160-sample frames):
#   c2enc 3200 <set>.pcm <tmp>.c2
#   strip the 7-byte header -> vectors/<set>.c2    # size must be 8 * frames
#   c2dec 3200 vectors/<set>.c2 vectors/<set>_c2dec.pcm
#   c2sim --rate 3200 --lspEWov <tmp>.bin <set>.pcm
#   -> vectors/<set>_params.csv
#
# c2enc writes a 7-byte header when the output name ends in .c2 (1 s of audio
# is 407 bytes: 7 + 50 * 8). c2dec 3200 accepts the headerless file. A name
# that ends in .c2 makes it print "Codec2 file specified but no header
# detected"; the PCM still matches a decode of the headered file, and this
# script checks that.
#
# vectors/UPSTREAM is rewritten from the kit's UPSTREAM (commit and build
# lines), plus a fixed generation date and the commands above. The date stays
# fixed so a second run is byte-identical.

set -euo pipefail
export LC_ALL=C
cd "$(dirname "$0")/.."

BB=${C2FX_BLACKBOX:-"$HOME/build/c2-blackbox"}
ENC=$BB/bin/c2enc
DEC=$BB/bin/c2dec
SIM=$BB/bin/c2sim

for tool in "$ENC" "$DEC" "$SIM" "$BB/UPSTREAM"; do
  if [[ ! -e "$tool" ]]; then
    echo "missing $tool" >&2
    exit 1
  fi
done

mkdir -p vectors
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

{
  grep -E '^(Upstream:|Commit:|Build:)' "$BB/UPSTREAM"
  echo "Generated: 2026-11-24"
  echo "Kit: a local black-box kit, located by \$C2FX_BLACKBOX"
  cat << 'EOF'
Commands:
  c2enc 3200 <set>.pcm <tmp>.c2
  strip the 7-byte header -> vectors/<set>.c2
  c2dec 3200 vectors/<set>.c2 vectors/<set>_c2dec.pcm
  c2sim --rate 3200 --lspEWov <tmp>.bin <set>.pcm
EOF
} > vectors/UPSTREAM

shopt -s nullglob
mapfile -t pcms < <(printf '%s\n' tests/data/voice/*.pcm tests/data/signals/*.pcm | sort)
if (( ${#pcms[@]} == 0 )); then
  echo "no pcm inputs" >&2
  exit 1
fi

for pcm in "${pcms[@]}"; do
  set_name=$(basename "$pcm" .pcm)
  bytes=$(wc -c < "$pcm")
  if (( bytes % 2 != 0 )); then
    echo "$pcm has an odd length" >&2
    exit 1
  fi
  samples=$(( bytes / 2 ))
  if (( samples % 160 != 0 )); then
    echo "$pcm is not a whole number of 160-sample frames" >&2
    exit 1
  fi
  frames=$(( samples / 160 ))

  "$ENC" 3200 "$pcm" "$tmp/$set_name.c2"
  enc_bytes=$(wc -c < "$tmp/$set_name.c2")
  expect=$(( 7 + 8 * frames ))
  if (( enc_bytes != expect )); then
    echo "c2enc wrote $enc_bytes bytes for $set_name, expected $expect (7-byte header + 8*frames)" >&2
    exit 1
  fi
  tail -c +8 "$tmp/$set_name.c2" > "vectors/$set_name.c2"
  c2_bytes=$(wc -c < "vectors/$set_name.c2")
  if (( c2_bytes != 8 * frames )); then
    echo "stripped $set_name.c2 is $c2_bytes bytes, expected $(( 8 * frames ))" >&2
    exit 1
  fi

  "$DEC" 3200 "$tmp/$set_name.c2" "$tmp/$set_name.hdr.pcm" 2>"$tmp/$set_name.hdr.dec.txt"
  "$DEC" 3200 "vectors/$set_name.c2" "vectors/${set_name}_c2dec.pcm" 2>"$tmp/$set_name.raw.dec.txt"
  if ! grep -q "no header detected" "$tmp/$set_name.raw.dec.txt"; then
    echo "c2dec did not report a missing header for headerless $set_name.c2" >&2
    cat "$tmp/$set_name.raw.dec.txt" >&2
    exit 1
  fi
  if ! cmp -s "$tmp/$set_name.hdr.pcm" "vectors/${set_name}_c2dec.pcm"; then
    echo "headerless c2dec output differs from the headered decode for $set_name" >&2
    exit 1
  fi
  dec_bytes=$(wc -c < "vectors/${set_name}_c2dec.pcm")
  if (( dec_bytes % 320 != 0 )); then
    echo "c2dec output for $set_name is not whole 160-sample frames ($dec_bytes bytes)" >&2
    exit 1
  fi

  "$SIM" --rate 3200 --lspEWov "$tmp/$set_name.bin" "$pcm" >/dev/null
  python3 - "$tmp/$set_name.bin" "vectors/${set_name}_params.csv" "$(( frames * 2 ))" << 'PY'
import struct
import sys

blob = open(sys.argv[1], "rb").read()
record = 92
if len(blob) % record != 0:
    raise SystemExit("lspEWov length is not a multiple of 92")
count = len(blob) // record
expected = int(sys.argv[3])
if count != expected:
    raise SystemExit("expected %d sub-frames, got %d" % (expected, count))

def fmt(value):
    text = format(value, ".9g")
    back = struct.unpack("<f", struct.pack("<f", float(text)))[0]
    if back != value:
        text = format(value, ".16g")
        back = struct.unpack("<f", struct.pack("<f", float(text)))[0]
        if back != value:
            raise SystemExit("cannot format %r" % (value,))
    return text

header = ["subframe", "w0", "voicing", "energy"]
header += ["lsp_%d" % index for index in range(10)]
lines = [",".join(header)]
for index in range(count):
    fields = struct.unpack_from("<23f", blob, index * record)
    voicing = fields[12]
    if voicing == 0.0:
        voice_text = "0"
    elif voicing == 1.0:
        voice_text = "1"
    else:
        voice_text = fmt(voicing)
    columns = [str(index), fmt(fields[11]), voice_text, fmt(fields[10])]
    columns += [fmt(fields[lsp]) for lsp in range(10)]
    lines.append(",".join(columns))
open(sys.argv[2], "w").write("\n".join(lines) + "\n")
PY

  dec_samples=$(( dec_bytes / 2 ))
  echo "$set_name pcm_frames=$frames c2_bytes=$c2_bytes dec_samples=$dec_samples dec_frames=$(( dec_samples / 160 )) subframes=$(( frames * 2 ))"
done

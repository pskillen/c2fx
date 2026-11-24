<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 The c2fx Authors -->

# Upstream 3200 vectors

Bitstream and decode of every file in `tests/data/voice/` and `tests/data/signals/`,
produced by upstream Codec 2 at the commit in `UPSTREAM`. The tools are run as
black boxes. CI does not rebuild upstream. These files change only when someone
reruns `tools/make_vectors.sh` and commits the result.

Speech inputs are synthesized. The made-up signals (tones, chirp, noise,
silence, DC, a clipped tone) are described in `tools/make_signals.py`.

## Files

For each `<set>`:

| File | Format |
|---|---|
| `<set>.c2` | 3200 bitstream, 8 bytes per 20 ms frame, no header |
| `<set>_c2dec.pcm` | That bitstream decoded: 16-bit signed little-endian, 8 kHz, mono, no header |
| `<set>_params.csv` | One row per 10 ms sub-frame: `w0`, voicing, energy, ten LSPs |

`c2enc 3200` writes a 7-byte header in front of the frames when the output
name ends in `.c2`. One second of audio is 407 bytes (7 + 50 × 8). The
committed `.c2` file is the frames after that header is removed. Its size is
8 times the number of 160-sample frames in the input.

`c2dec 3200` accepts that headerless file. Because the name still ends in
`.c2`, it prints `Codec2 file specified but no header detected` and decodes
it anyway. The PCM matches a decode of the headered file; `tools/make_vectors.sh`
checks that before keeping the output. On these inputs the decode has the
same number of samples as the PCM that was encoded.

## Parameter CSV

`c2sim --rate 3200 --lspEWov <file> <set>.pcm` writes one 92-byte record per
10 ms sub-frame (two records per 20 ms codec frame), 23 little-endian
float32 values:

| Floats | CSV column | What a black-box comparison showed |
|---|---|---|
| 0–9 | `lsp_0` … `lsp_9` | Ten LSP values. Unchanged if `--rate 3200` is left off |
| 10 | `energy` | Matches column 4 of `--dump_pitch_e` when `--rate` is left off. With `--rate 3200` it moves |
| 11 | `w0` | Matches column 1 of `--dump_pitch_e` when `--rate` is left off. With `--rate 3200` it moves. The floor on silence is 0.03927, which is `2 * pi * 50 / 8000` |
| 12 | `voicing` | 0 or 1. Matches column 3 of `--dump_pitch_e` with or without `--rate 3200` |
| 13–22 | not stored | Zero unless `--rate` is set. Not interpreted here |

`--dump_pitch_e` was the cross-check. It has pitch and energy, not the LSPs.
`--lsp` and `--modelout` do not add a text LSP dump. `--modelout` writes a
1300-byte binary record per sub-frame and was left unused.

## Regenerating

```
tools/make_vectors.sh
```

The kit defaults to `~/build/c2-blackbox`. `C2FX_BLACKBOX` overrides that
directory. The script reads the kit's `UPSTREAM` for the commit and build
lines, and refuses a `c2enc` output whose size is not 7 plus 8 bytes per frame.

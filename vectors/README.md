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

`high_voice_c2dec.pcm` has five samples at 32767 in upstream's decode output;
that saturation comes from upstream, not from the input PCM.

## Parameter CSV

`c2sim --rate 3200 --lspEWov <file> <set>.pcm` writes one 92-byte record per
10 ms sub-frame (two records per 20 ms codec frame), 23 little-endian
float32 values:

| Floats | CSV column | Meaning |
|---|---|---|
| 0–9 | `lsp_0` … `lsp_9` | Ten analysis LSPs (radians). Unquantized; unchanged when `--rate 3200` is on |
| 10 | `energy` | Post-3200 energy (what the decoder path sees), not the analysis energy in `--dump_pitch_e` |
| 11 | `w0` | Post-3200 pitch (rad/sample), not the analysis pitch in `--dump_pitch_e` |
| 12 | `voicing` | 0 or 1 |
| 13–22 | not stored | Zero unless `--rate` is set. Not interpreted here |

The committed CSV stores quantized `w0` and `energy` plus unquantized LSPs.
Compared with `--dump_pitch_e` on the same PCM, the analysis pitch and energy
differ by up to about 1e-3 rad in `w0` and by tens of units in `energy`
(for example sub-frame 40 on `low_voice`: CSV energy 79.0 vs analysis 93.1;
unvoiced floors 0.1 in the CSV vs 0.0005 in the pitch dump). On silence,
`w0` in the CSV stays at 0.03927, which is `2 * pi * 50 / 8000`.

Column layout was cross-checked with both tools at `--rate 3200` (required).
`--lspEWov` without `--rate` aborts on `low_voice`; `make_vectors.sh` always
passes `--rate 3200`. Neither `--dump_pitch_e` nor the four-column pitch file
works without `--rate 3200` (empty file or a single energy column only).

On `test_transmission.pcm` and `low_voice.pcm`:

```text
c2sim --rate 3200 --lspEWov lsp.bin <set>.pcm
c2sim --rate 3200 --lpc 10 --dump_pitch_e pitch.txt <set>.pcm
```

Compare `lsp.bin` float 11 to column 1 of `pitch.txt`, float 12 to column 3,
and float 10 to column 4. Maximum absolute difference on both inputs:
about 1.05e-3 on `w0`, 0 on voicing. At `--rate 3200`, float
10 is not the same number as column 4 (quantized vs analysis), which is why
the CSV energy column follows `lsp.bin`, not the pitch dump.

`--dump_pitch_e` has pitch and energy only, not LSPs. `--lsp` and `--modelout`
do not add a text LSP dump. `--modelout` writes a 1300-byte binary record per
sub-frame and was left unused.

## Regenerating

```
tools/make_vectors.sh
```

The kit path comes from `C2FX_BLACKBOX` (see `tools/make_vectors.sh`). The
script reads the kit's `UPSTREAM` for the commit and build
lines, and refuses a `c2enc` output whose size is not 7 plus 8 bytes per frame.

<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 The c2fx Authors -->
# Black-box experiments

Scripts that run the upstream Codec 2 tools **as black boxes** (`c2enc`, `c2dec`, `c2sim` from the kit)
with crafted input, and print the numbers the spec's facts rest on. They never open upstream source.
The spec ([`docs/spec/c2-3200.md`](../../docs/spec/c2-3200.md)) cites each by its `[exp: tools/blackbox/<script>]`
tag. Nothing generated is committed.

## Setup

- The kit: set `C2FX_BLACKBOX` to the kit directory (required; the scripts exit with a message if it is unset; the kit is described in `docs/clean-room.md`): `bin/c2enc`, `bin/c2dec`,
  `bin/c2sim` (wrappers), `codebook/*.txt` (read as lists of numbers).
- Scratch: `C2FX_WORK` (default `./work`). Generated files go there, and only there.
- Python 3 with **numpy** (and nothing else): `python3 -m venv venv && venv/bin/pip install numpy`.
- Run one: `C2FX_WORK=/tmp/c2 python3 x02_pitch_field.py`. Run all: `./run_all.sh venv/bin/python`.
  The whole suite takes about a minute.

`c2bb.py` is the shared helper (run the tools, pack frames, Gray code, textbook LSP to LPC, harmonic
measurement). It is analysis code for experiments, not library code, and the implementation must not copy it:
the spec states every formula it uses.

## Experiments

| Script | What it does | Fact it settles |
|---|---|---|
| `x01_header_and_layout.py` | Encodes with `.c2` and `.bin` names; flips each of the 64 bits of a voiced frame and decodes | 7-byte file header only for `.c2` (`c0 de c2 01 02 <mode> 00`, mode byte 0 for 3200); `c2dec` accepts both; frame is 8 bytes; bits 0-1 and 2-8 change the pitch and voicing, 9-13 the level, 14-63 the envelope |
| `x02_pitch_field.py` | Decodes steady frames for chosen w0 indices, fits the harmonic frequencies | w0 is bits 2-8, Gray coded, f0 = 50 + 350*i/128 Hz exactly; harmonics are exactly m*f0; `c2enc` uses the same law |
| `x03_energy_field.py` | Sweeps the 32 energy values; relates output power to the 512-bin band sum of 1/\|A\|^2 | Energy is bits 9-13, Gray coded, 1.5625 dB per step; the level law and its constant |
| `x04_lsp_fields.py` | Sweeps each of the ten 5-bit groups; regresses measured harmonic levels on the model | Ten Gray-coded 5-bit indices, group j into `dlsp<j+1>.txt` (Hz), LSPs are the running sum; the envelope is 1/\|A\|^1.2-like (post filter) plus a step below 1 kHz |
| `x05_post_filter.py` | `--nopf` check; grid of post-filter variants against measured levels | `--nopf` has no effect at 3200; best variants and how weakly gamma/beta are pinned |
| `x06_first_harmonic.py` | Steady frames across w0 indices 33-40 | Harmonic 1 is scaled by 0.032 (about -30 dB) when f0 < 150 Hz (index 36 reduced, 37 full level) |
| `x07_interpolation.py` | A-to-B frame transitions with a steady w0; follows one harmonic's amplitude (part C: the ramp at 10-sample resolution) | The first sub-frame's energy is the geometric mean (dB midpoint) of previous and current; its LSPs are the Hz midpoint; ramp knots are at the sub-frame centres |
| `x08_voicing_and_state.py` | Voicing bit patterns, determinism, start-up, extreme indices, all 128 pitch indices | Observations that remain open questions (voicing rule, start-up state, out-of-range LSPs); every pitch index decodes periodic |
| `x09_post_filter_fit.py` | Fits the post-filter exponents with the full envelope chain; the energy constant | `beta` 0.2-0.35 with `gamma` 0.3-0.7 (best 0.275 at 0.6, 0.74 dB rms); the energy offset |
| `x10_w0_interpolation.py` | Harmonic-comb pitch of sub-frame a after voiced and unvoiced frames; start-up | `w0` midpoint if the previous sub-frame was voiced, current `w0` otherwise |
| `x11_w0_quant.py` | Pulse trains and a pitch glide, compared with `c2sim --dump_pitch_e` | w0 index is the nearest grid point of the second 10 ms frame's analysis pitch; raw index 128 clips to 127 |
| `x12_energy_quant.py` | Amplitude sweep, silence, full scale, loud/quiet blocks | Energy index is the nearest step of (10 log10 E + 10) * 32/50 on that same 10 ms frame; non-positive E is index 0 |
| `x13_lsp_quant.py` | Closed-loop versus open-loop search on pulses, noise, a low sine, and the synthesized voice files | LSP indices are nearest-Hz differences off the quantized running sum; a target below 25 Hz takes index 0 |
| `x14_corpus_fields.py` | Every PCM file under `tests/data`, all four field rules, both voicing bits | All 2260 frames match; the even 10 ms row does not |
| `x15_analysis_window.py` | Impulses, a pulse-train vowel, a 20 Hz sine, against an order-10 LPC gain | 279-point Hann window centred at sample (s-1)*80, sum of squares 1/512; the sine does not follow this gain |
| `x16_pitch_behaviour.py` | Pulse trains from 55 to 600 Hz, and pure tones from 50 to 400 Hz | Pulse trains inside 50-400 Hz track; pure tones do not come back at the tone frequency |
| `x17_voicing_bits.py` | Noise, pulses, and an 180 Hz tone plus noise at several SNRs | Bit 0 and bit 1 are the two 10 ms voicing flags; the mixture does not switch at 6 dB |
| `x18_energy_crosslink.py` | The quantized energies in `vectors/*_params.csv` against `10^((-10 + e*50/32)/10)`; the decoder's level offset; refit with the offset fixed | `E_MIN_DB` = -10 dB, `E_MAX_DB` = 40 dB; synthesis gain 6.38 dB (best pair) or 5.91 dB (PDF pair), about a factor 2 |

`c2sim` facts (I-3) were checked by hand, not scripted: `c2sim <raw> -o <out> --modelout <file>` writes one
1300-byte record per 10 ms analysis frame (`float32 w0`, `int32 L`, `float32 A[161]`, `float32 phi[161]`,
`int32 voiced`, little endian: this layout is inferred from the record size and the values, not from source);
with `--rate 3200` the w0 in the records is already quantised on the 3200 grid (index 37 gave 151.172 Hz).
`x11` through `x17` drive `c2sim --rate 3200` with `--lspEWov` and with `--lpc 10 --dump_pitch_e`.
The pitch-dump columns, in order, are analysis `w0` (radians/sample), a second number that is not the
voicing decision, the voicing flag (0 or 1), and the analysis energy. `--lspEWov` is the 92-byte record
in `vectors/README.md`: ten unquantized LSP frequencies in radians, then the quantized energy and `w0`.

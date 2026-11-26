<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 The c2fx Authors -->

# c2fx

A fixed-point implementation of the Codec 2 3200 bit/s mode, written from the published description; not affiliated with the Codec 2 project.

## Status

Pre-release. The fixed-point library encodes and decodes 3200 bit/s frames.

## Goals

- Bitstream-compatible with the Codec 2 3200 mode as used by M17.
- No floating point, no libm, no heap, and no global variables.
- C99, with all state kept in caller-provided structs.

## Layout

Paths not yet in the tree are planned.

| Path | What it holds |
|---|---|
| `include/c2fx.h` | Public header |
| `src/` | Portable fixed-point code |
| `src/dsp/` | This library's own FFT and trigonometry, left out when a host supplies them |
| `src/arch/armv7em/`, `src/arch/csky/` (planned) | Architecture-specific kernels |
| `src/tables/` | Generated quantizer tables (`c2_3200_tab.c`) |
| `model/` | Floating-point oracle, for the host only, never linked into a product build |
| `tests/` | Host tests |
| `vectors/` | Committed test vectors |
| `tools/` | Scripts for vectors, tables, size, and symbol checks |
| `docs/spec/` | Algorithm specification |
| `docs/clean-room.md` | Clean-room roles and the reading log |

## Build and test

GNU make, a C99 compiler, and binutils (`size` and `nm`).

The build stamps `c2fx_version()` from the exact Git tag (dropping a leading `v`), the short commit SHA, or `local` when Git metadata is unavailable. Set `C2FX_VERSION` to override it.

```sh
make
make test
make sanitize
make wav
make wavfx
make enc
make encfx
make wavenc
make cross-arm
make cross-csky
make cross-csky CSKY_DSP=1
```

`make` writes `build/host/libc2fx.a`. `make test` builds that archive and runs the host tests. `make sanitize` runs the same tests with `-fsanitize=address,undefined`. `make wav` builds `build/c2m_dec` and writes 8 kHz mono PCM under `build/wav/` for each committed vector bitstream. `make wavfx` builds `build/c2fx_dec` and writes the same bitstreams, decoded by the fixed-point decoder, under `build/wavfx/`. Run it by hand as `build/c2fx_dec <in.c2> <out.pcm>`: the output is int16 little-endian, 8 kHz, mono. `make enc` builds `build/c2m_enc` and runs it on each test voice and signal file, writing `build/enc/<set>.c2`. Run it by hand as `build/c2m_enc <in.pcm> <out.c2>`: the input is int16 little-endian, 8 kHz, mono, a whole number of 160-sample frames.

`make encfx` builds `build/c2fx_enc` and writes the fixed-point bitstreams to
`build/encfx/`. Run `build/c2fx_enc <in.pcm> <out.c2>` with the same PCM and raw
bitstream formats as the model. `make wavenc` decodes those bitstreams with
the fixed-point decoder and writes WAVs to `build/wavenc/`.

`make C2FX_NO_ENCODER=1` builds a decoder-only archive in
`build/host-decoder/`. Set `C2FX_NO_ENCODER=1` on a cross build to get an
archive in the corresponding `-decoder` directory. Compile applications
against these archives with `-DC2FX_NO_ENCODER`.

`make archive EXTERNAL_DSP=1` writes `build/host-external-dsp/libc2fx.a`.
Link it with implementations of the five hooks in [c2fx_dsp.h](src/dsp/c2fx_dsp.h).
`make test-external-dsp` checks the archive's symbols and compares the decoded
PCM and vector CRCs.

`make crc` refreshes [the bench CRCs](vectors/expected_crc.txt).
After that build, `build/c2fx_crc <file.c2>...` prints the basename,
frame count, and CRC-32 of the decoded little-endian PCM for each file.

`make cross-arm` writes `build/arm/libc2fx.a` for Cortex-M4, Thumb, soft-float (`-mcpu=cortex-m4 -mthumb -mfloat-abi=soft -Os -ffunction-sections -fdata-sections`). `make cross-csky` writes `build/csky/libc2fx.a` for CK803S (`-mcpu=ck803s -Os -ffunction-sections -fdata-sections`). `CSKY_DSP=1` adds `-Wa,-mdsp` and writes `build/csky-dsp/libc2fx.a`.

`ARM_PREFIX` and `CSKY_PREFIX` override the cross tool prefixes. Each prefix is the tool name stem including the trailing hyphen; the defaults are `arm-none-eabi-` and `csky-elf-` from `PATH`. To use an install tree instead, pass the path through the tool name, for example `make cross-arm ARM_PREFIX=$HOME/opt/arm-gnu-toolchain-13.3.rel1-x86_64-arm-none-eabi/bin/arm-none-eabi-`.

```sh
tools/size_report.sh arm
tools/check_no_float.sh build/arm "${ARM_PREFIX}"
tools/size_report.sh csky
tools/check_no_float.sh build/csky "${CSKY_PREFIX}"
tools/size_report.sh csky-dsp
tools/check_no_float.sh build/csky-dsp "${CSKY_PREFIX}"
```

`size_report.sh` prints the text-plus-data size of each object and exits non-zero when a budget in `tools/budgets.txt` is exceeded. Pass a cross `size` as the second argument when the host `size` cannot read the objects. `check_no_float.sh` exits non-zero when an object has an undefined symbol that is not defined elsewhere in the build and not listed in `tools/allowed_undefined.txt`.

## Library API

Allocate a state object and a `c2fx_scratch_t` using the declarations in
[the public header](include/c2fx.h). Initialize a decoder with
`c2fx_dec_init` and call `c2fx_decode` for each 8-byte frame to produce
160 PCM samples. Pass a null bitstream pointer for a lost frame.

Initialize an encoder with `c2fx_enc_init` and call `c2fx_encode` for each
160-sample PCM frame. The first call produces an encoded silence frame.
Subsequent calls return the delayed bitstream, one frame (160 samples)
behind the input. For a file, omit the startup frame and send a zero-filled
frame after the last input to drain the final bitstream frame. Encoder and
decoder calls made from one task can reuse the same scratch object, which is clobbered on each call.

## License

[MIT](LICENSE). [NOTICE](NOTICE) records the origin of the quantizer tables in `src/tables/c2_3200_tab.c`.

<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 The c2fx Authors -->

# c2fx

A fixed-point implementation of the Codec 2 3200 bit/s mode, written from the published description; not affiliated with the Codec 2 project.

## Status

Pre-release. The tree has its layout and continuous integration. The codec is not implemented yet.

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
| `src/dsp/` (planned) | This library's own FFT and trigonometry, left out when a host supplies them |
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

```sh
make
make test
make sanitize
make wav
make cross-arm
make cross-csky
make cross-csky CSKY_DSP=1
```

`make` writes `build/host/libc2fx.a`. `make test` builds that archive and runs the host tests. `make sanitize` runs the same tests with `-fsanitize=address,undefined`. `make wav` builds `build/c2m_dec` and writes 8 kHz mono PCM under `build/wav/` for each committed vector bitstream.

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

## License

[MIT](LICENSE). [NOTICE](NOTICE) records the origin of the quantizer tables in `src/tables/c2_3200_tab.c`.

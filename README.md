A fixed-point implementation of the Codec 2 3200 bit/s mode, written from the published description; not affiliated with the Codec 2 project.

<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 The c2fx Authors -->

## Status

Pre-release. The tree has its layout and continuous integration. The codec is not implemented yet.

## Goals

- Bitstream-compatible with the Codec 2 3200 mode as used by M17.
- No floating point, no libm, no heap, and no global variables.
- C99, with all state kept in caller-provided structs.

## Layout

| Path | What it holds |
|---|---|
| `include/c2fx.h` | Public header |
| `src/` | Portable fixed-point code |
| `src/dsp/` | This library's own FFT and trigonometry, left out when a host supplies them |
| `src/arch/armv7em/`, `src/arch/csky/` | Architecture-specific kernels |
| `src/tables/` | Generated quantizer tables |
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
make cross-arm
make cross-csky
make cross-csky CSKY_DSP=1
```

`make` writes `build/host/libc2fx.a`. `make test` builds that archive and runs the host tests. `make sanitize` runs the same tests with `-fsanitize=address,undefined`.

`make cross-arm` writes `build/arm/libc2fx.a` for Cortex-M4, Thumb, soft-float (`-mcpu=cortex-m4 -mthumb -mfloat-abi=soft -Os -ffunction-sections -fdata-sections`). `make cross-csky` writes `build/csky/libc2fx.a` for CK803S (`-mcpu=ck803s -Os -ffunction-sections -fdata-sections`). `CSKY_DSP=1` adds `-Wa,-mdsp`.

`ARM_PREFIX` and `CSKY_PREFIX` override the cross tool prefixes. Each prefix is the path through the tool name, including the trailing hyphen. The defaults are Arm GNU Toolchain 13.3.Rel1 at `~/opt/arm-gnu-toolchain-13.3.rel1-x86_64-arm-none-eabi/bin/arm-none-eabi-` and C-SKY GCC at `~/toolchains/csky-elf/bin/csky-elf-`.

```sh
tools/size_report.sh arm
tools/check_no_float.sh build/arm "${ARM_PREFIX}"
tools/size_report.sh csky
tools/check_no_float.sh build/csky "${CSKY_PREFIX}"
```

`size_report.sh` prints the text-plus-data size of each object and exits non-zero when a budget in `tools/budgets.txt` is exceeded. `check_no_float.sh` exits non-zero when an object references a soft-float helper, a libm function, or the heap.

## License

[MIT](LICENSE). [NOTICE](NOTICE) records the quantizer tables' origin. Those tables are not in the tree yet.

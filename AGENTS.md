<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 The c2fx Authors -->

# Notes for coding agents

c2fx is an MIT-licensed fixed-point library for the Codec 2 3200 bit/s mode used by M17. It is a separate project, written from the published description of that mode. It is not the Codec 2 project and not a fork of it.

Human contributors follow the same rules. The build commands are in [README.md](README.md). The clean-room record, the upstream-tool pin, and the reading log are in [docs/clean-room.md](docs/clean-room.md).

## Hard rules

1. **Clean room.** Work from this repository's specification, published descriptions, datasheets, and permissively licensed code such as RadioFoundry (MIT). Do not open, search, or copy the paths under [Never open](#never-open), and do not open upstream C sources. If a task seems to need any of that, stop and ask the owner. **An agent that has read restricted source never implements.**

2. **SPDX header on every new file**, in the style of its neighbors: `// SPDX-License-Identifier: MIT` and `// Copyright (c) 2026 The c2fx Authors` (`#` for scripts, makefiles, and YAML; `<!-- -->` for Markdown).

3. **Zero warnings.** Every compile uses `-Wall -Wextra -Wpedantic -Werror`. A configuration the Makefile or CI builds has to be clean.

4. **US English** in code, comments, documentation, and commit messages.

## Language and state

Library code is C99 and includes only `stdint.h` and `stddef.h`. No heap, no floating point, no libm, and no global variables. All state lives in structs the caller provides and passes in.

## Speech

Tests use synthesized speech only. A recording of a person does not enter this repository. The two phrases taken from RadioFoundry stay attributed to The RadioFoundry Authors (see `REUSE.toml`).

## Roles

| Role | May read | May run | Writes | Does not |
|---|---|---|---|---|
| Spec writer | Published descriptions: the Codec 2 paper read as a document, with nothing copied from its text, figures, or listings; Rowe's 1997 thesis on harmonic sinusoidal coding; rowetel.com posts; McAulay and Quatieri; Kabal and Ramachandran; textbook LPC; the upstream README, file listing, and license; upstream codebook data files, as numbers | Upstream tools as black boxes (`c2enc`, `c2dec`, `c2sim`), fed with inputs and bit patterns made for the test | `docs/spec/c2-3200.md`: prose, formulas, constants, the bit layout, and tables stated as facts. Rows in the reading log | Upstream `.c` and `.h` files, Codec2-mod, the Rust crate, the dm32-m17 codec, and the earlier fixed-point kernels in `baofeng-dm32-firmware` |
| Source reader | One named upstream file, only after the owner agrees, and only for one question the documents and the black-box runs could not settle | Nothing | That fact, as a number or one sentence, in the reading log, with the file name and the commit | Writing code, or writing any other part of the specification |
| Table importer | Upstream codebook data files only: lists of numbers | `tools/import_tables.py`, written in this repository | `src/tables/` and the provenance that goes with it | Opening `.c` or `.h` files |
| Implementer | This repository's specification, tables, vectors, and float model; RadioFoundry's MIT sources; public DSP literature | Vector and test scripts, which may call the upstream tools as black boxes | All of the library code | Everything in the spec writer's "does not" column, and upstream source of any kind |

An implementer is a fresh session whose brief cites this repository. The specification for a half of the codec is reviewed and merged before implementation of that half starts. The reading log names the specification revision that phase was built from. When a black-box run settles a fact, the specification says how, so the run can be repeated.

## Never open

Do not open, search, or read:

- Upstream Codec 2 source (`.c`, `.h` and build files), wherever it is checked out or built, except as the source-reader role allows
- The `dm32-m17` firmware (GPL-3.0) and its codec
- The earlier fixed-point Codec 2 kernels in the owner's `baofeng-dm32-firmware` repository (LGPL-derived)
- OpenRTX
- Any Codec 2 implementation, on the web or on disk

RadioFoundry (https://github.com/RadioFoundry/RadioFoundry) is MIT and may be read.

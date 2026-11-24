<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 The c2fx Authors -->

# Clean room

c2fx is original work under the MIT license. The implementation does not derive from the Codec 2 project's LGPL sources. The quantizer tables, when they arrive, are interoperability data and are the only files with that origin. [NOTICE](../NOTICE) states their status.

Four roles keep the people who read restricted material away from the people who write code. An agent that has read restricted source never implements. An implementer is a fresh session, and its brief cites this repository's specification and files, not upstream source.

## Roles

| Role | May read | May run | Writes | Does not |
|---|---|---|---|---|
| Spec writer | Published descriptions: the Codec 2 paper read as a document, with nothing copied from its text, figures, or listings; Rowe's 1997 thesis on harmonic sinusoidal coding; rowetel.com posts; McAulay and Quatieri; Kabal and Ramachandran; textbook LPC; the upstream README, file listing, and license; upstream codebook data files, as numbers | Upstream tools as black boxes (`c2enc`, `c2dec`, `c2sim`), fed with inputs and bit patterns made for the test | `docs/spec/c2-3200.md`: prose, formulas, constants, the bit layout, and tables stated as facts. Rows in the reading log | Upstream `.c` and `.h` files, Codec2-mod, the Rust crate, the dm32-m17 codec, and the earlier fixed-point kernels in `baofeng-dm32-firmware` |
| Source reader | One named upstream file, only after the owner agrees, and only for one question the documents and the black-box runs could not settle | Nothing | That fact, as a number or one sentence, in the reading log, with the file name and the commit | Writing code, or writing any other part of the specification |
| Table importer | Upstream codebook data files only: lists of numbers | `tools/import_tables.py`, written in this repository | `src/tables/` and the provenance that goes with it | Opening `.c` or `.h` files |
| Implementer | This repository's specification, tables, vectors, and float model; RadioFoundry's MIT sources; public DSP literature | Vector and test scripts, which may call the upstream tools as black boxes | All of the library code | Everything in the spec writer's "does not" column, and upstream source of any kind |

The specification for a half of the codec is reviewed and merged before implementation of that half starts. The reading log names the specification revision each implementation phase was built from. When a black-box run settles a fact, the specification says how ("found by encoding pure tones and observing bits 0-6" or whatever the run was), so a reviewer can repeat it.

## Never open

Do not open, search, or read:

- Upstream Codec 2 source (`.c`, `.h` and build files), wherever it is checked out or built
- The `dm32-m17` firmware (GPL-3.0) and its codec
- The earlier fixed-point Codec 2 kernels in the owner's `baofeng-dm32-firmware` repository (LGPL-derived)
- OpenRTX
- Any Codec 2 implementation, on the web or on disk

RadioFoundry (https://github.com/RadioFoundry/RadioFoundry) is MIT and may be read.

## Upstream tools

The pin is commit `310777b1c6f1af0bc7c72f5b32f80f6fd9136962` of <https://github.com/drowe67/codec2>.

The maintainer builds upstream at that commit outside every repository and gives agents a **black-box kit** holding only the built `c2enc`, `c2dec`, `c2sim`, the shared library they need, upstream's codebook data files (lists of numbers), `codec2.pdf`, the upstream README and licence, and a file recording the commit and build. Agents run the tools and read only those files. The spec writer and the table importer may read the codebook data files as numbers; implementers need not. Scripts locate the kit through the `C2FX_BLACKBOX` environment variable.

## Reading log

No entries yet.

| Date | Agent | Role | What was read or run | Why | Owner OK (for source reads) | Spec revision |
|---|---|---|---|---|---|---|

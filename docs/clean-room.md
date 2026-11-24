<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 The c2fx Authors -->

# Clean room

c2fx is original work under the MIT license. The implementation does not derive from the Codec 2 project's LGPL sources. The quantizer table values in `src/tables/c2_3200_tab.c` are interoperability data and are the only source whose numeric content has that origin. [NOTICE](../NOTICE) states their status.

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

- Upstream Codec 2 source (`.c`, `.h` and build files), wherever it is checked out or built, except as the source-reader role allows
- The `dm32-m17` firmware (GPL-3.0) and its codec
- The earlier fixed-point Codec 2 kernels in the owner's `baofeng-dm32-firmware` repository (LGPL-derived)
- OpenRTX
- Any Codec 2 implementation, on the web or on disk

RadioFoundry (https://github.com/RadioFoundry/RadioFoundry) is MIT and may be read.

## Upstream tools

The pin is commit `310777b1c6f1af0bc7c72f5b32f80f6fd9136962` of <https://github.com/drowe67/codec2>.

The maintainer builds upstream at that commit outside every repository and gives agents a **black-box kit** holding only the built `c2enc`, `c2dec`, `c2sim`, the shared library they need, upstream's codebook data files (lists of numbers), `codec2.pdf`, the upstream README and licence, and a file recording the commit and build. Agents run the tools and read only those files. The spec writer and the table importer may read the codebook data files as numbers; implementers need not. Scripts locate the kit through the `C2FX_BLACKBOX` environment variable.

## Reading log

| Date | Agent | Role | What was read or run | Why | Owner OK (for source reads) | Spec revision |
|---|---|---|---|---|---|---|
| 2026-11-24 | spec writer | Spec writer | S1: `doc/codec2.pdf` in the black-box kit (upstream commit `310777b1`, document revision `ff00a6e`, February 2025), read as a document, text extracted with `pdftotext`; nothing copied | Frame allocation, mode table, post-filter and phase-model structure, synthesis | n/a (no source read) | c2-3200.md rev 2 |
| 2026-11-24 | spec writer | Spec writer | S2: Kit `codebook/*.txt`: read as lists of numbers: `dlsp1`..`dlsp10` in full; headers and first values of `lsp1`, `lsp10`, `lsp8910`, `gecb`, `lspjmv1` | Which files hold the 3200 tables and their values | n/a (no source read) | c2-3200.md rev 2 |
| 2026-11-24 | spec writer | Spec writer | S3: `doc/README.md`, `doc/COPYING` in the kit; upstream's file listing from the PDF's table 3 | Licence and which file each open question would need (open questions) | n/a (no source read) | c2-3200.md rev 2 |
| 2026-11-24 | spec writer | Spec writer | S4: `UPSTREAM`, `SHA256SUMS` in the kit | Pinned commit and build of the black boxes | n/a (no source read) | c2-3200.md rev 2 |
| 2026-11-24 | spec writer | Spec writer | E1: `tools/blackbox/x01_header_and_layout.py`: `c2enc` header per mode; `c2dec` on headered/headerless input; flip each of 64 bits of a frame and decode | Header and which bits do what | n/a (no source read) | c2-3200.md rev 2 |
| 2026-11-24 | spec writer | Spec writer | E2: `x02_pitch_field.py`: decode 20 chosen w0 indices, fit harmonic frequencies; encode synthetic voiced input at known f0 | w0 field position, Gray code, law, exactness | n/a (no source read) | c2-3200.md rev 2 |
| 2026-11-24 | spec writer | Spec writer | E3: `x03_energy_field.py`: sweep the 32 energy values; band-sum offset across w0 | Energy step; level law; 512-bin band sum | n/a (no source read) | c2-3200.md rev 2 |
| 2026-11-24 | spec writer | Spec writer | E4: `x04_lsp_fields.py`: sweep each of ten 5-bit groups; regress random frames on the LSP to LPC model | LSP group order, tables, deltas, post-filter terms | n/a (no source read) | c2-3200.md rev 2 |
| 2026-11-24 | spec writer | Spec writer | E5: `x05_post_filter.py`: `--nopf` equality; 180-variant grid; part C: envelope DFT size scan | Post filter always on; constants not pinned | n/a (no source read) | c2-3200.md rev 2 |
| 2026-11-24 | spec writer | Spec writer | E6: `x06_first_harmonic.py`: steady frames at indices 33-40 | First harmonic dropped below about 150 Hz | n/a (no source read) | c2-3200.md rev 2 |
| 2026-11-24 | spec writer | Spec writer | E7: `x07_interpolation.py`: steady A then B, follow a harmonic's amplitude | Energy and LSP interpolation weights; sub-frame timing | n/a (no source read) | c2-3200.md rev 2 |
| 2026-11-24 | spec writer | Spec writer | E8: `x08_voicing_and_state.py`: voicing patterns, determinism, start-up, extreme LSPs, all 128 pitches | Voicing observations (OQ-1), start-up (OQ-7), extremes (OQ-8) | n/a (no source read) | c2-3200.md rev 2 |
| 2026-11-24 | spec writer | Spec writer | E9: `c2sim <raw> --modelout`, `--rate 3200` (by hand, not scripted) | Model dump record size and w0 grid | n/a (no source read) | c2-3200.md rev 2 |
| 2026-11-24 | spec writer | Spec writer | L1: No literature was opened. Sections 5 and 7 rest on the PDF and on experiment; the minimum-phase construction is standard textbook material, not consulted | Recorded so the log is honest: nothing outside the kit's `doc/` and `codebook/` was read | n/a (no source read) | c2-3200.md rev 2 |
| 2026-11-24 | P2b table importer (Composer) | Table importer | Upstream codebook `dlsp1.txt` … `dlsp10.txt` (lists of integers) at commit `310777b1c6f1af0bc7c72f5b32f80f6fd9136962`, via the black-box kit; SHA-256 checked in `tools/import_tables.py` | Import the 3200 LSP quantizer tables into `src/tables/` as interoperability data | n/a (no source read) | c2-3200.md rev 2 |

## Open questions

The spec lists OQ-1 to OQ-9 (`docs/spec/c2-3200.md`, "Open questions"): facts the documents and black-box runs did not settle. They await the owner's decision on source reads (role "Source reader"); none has been read.

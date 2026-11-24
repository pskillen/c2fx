<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 The c2fx Authors -->

# Quantizer tables

`c2_3200_tab.c` and `c2_3200_tab.h` hold the ten 5-bit LSP quantizer codebooks used by Codec 2 at 3200 bit/s (`c2fx_lsp_tab[10][32]`, values in Hz).

`c2_3200_tab.c` is the only source file whose values come from upstream Codec 2 data files; `vectors/` holds upstream tool output committed as test data. The implementation otherwise follows this repository's specification; see [NOTICE](../../NOTICE) and [docs/clean-room.md](../../docs/clean-room.md).

## Regenerate

From the repository root, with the black-box kit on `C2FX_BLACKBOX`:

```sh
export C2FX_BLACKBOX=/path/to/c2-blackbox
python3 tools/import_tables.py
python3 tools/import_tables.py --check
```

The importer reads only the kit's `codebook/dlsp1.txt` … `dlsp10.txt` data files. It verifies SHA-256, header `1 32`, and value range before writing `src/tables/`.

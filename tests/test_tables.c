// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The c2fx Authors

#include "c2_3200_tab.h"

#include <stdio.h>
#include <stdint.h>

#define LSP_COUNT 10
#define ENTRIES_PER_LSP 32

static void ladder_short(int16_t out[ENTRIES_PER_LSP])
{
    for (int index = 0; index < ENTRIES_PER_LSP; index++) {
        out[index] = (int16_t)(25 + index * 25);
    }
}

static void ladder_long(int16_t out[ENTRIES_PER_LSP])
{
    int index = 0;
    for (int value = 25; value <= 200; value += 25) {
        out[index++] = (int16_t)value;
    }
    for (int value = 250; value <= 1400; value += 50) {
        out[index++] = (int16_t)value;
    }
}

static int uses_short_ladder(int table)
{
    return table == 0 || table == 1 || table == 2 || table >= 6;
}

int main(void)
{
    int16_t expect[ENTRIES_PER_LSP];

    for (int table = 0; table < LSP_COUNT; table++) {
        if (uses_short_ladder(table)) {
            ladder_short(expect);
        } else {
            ladder_long(expect);
        }

        for (int index = 0; index < ENTRIES_PER_LSP; index++) {
            if (c2fx_lsp_tab[table][index] != expect[index]) {
                fprintf(
                    stderr,
                    "LSP %d entry %d is %d, expected %d\n",
                    table + 1,
                    index,
                    (int)c2fx_lsp_tab[table][index],
                    (int)expect[index]
                );
                return 1;
            }
        }

        for (int index = 0; index < ENTRIES_PER_LSP - 1; index++) {
            if (c2fx_lsp_tab[table][index] >= c2fx_lsp_tab[table][index + 1]) {
                fprintf(
                    stderr,
                    "LSP %d not strictly increasing at %d (%d, %d)\n",
                    table + 1,
                    index,
                    (int)c2fx_lsp_tab[table][index],
                    (int)c2fx_lsp_tab[table][index + 1]
                );
                return 1;
            }
        }
    }

    return 0;
}
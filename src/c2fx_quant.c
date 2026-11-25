// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The c2fx Authors
// Spec 3 and 4: dequantizers and the sub-frame interpolation.

#include "c2fx_internal.h"

#include "c2_3200_tab.h"

/* log2(10^((-10 + q1 * 25/32) / 10)) in Q11.
 * q1 is twice an energy index. Sub-frame b uses 2*e. Sub-frame a uses the
 * sum of the two indices, which is the dB midpoint. Entry q1 = 0 is -10 dB
 * (linear power 0.1); entry 62 is index 31 (38.4375 dB).
 */
static const int16_t energy_log2_q11[63] = {
    -6803, -6272, -5740, -5209, -4677, -4146, -3614, -3083,
    -2551, -2020, -1488, -957, -425, 106, 638, 1169,
    1701, 2232, 2764, 3295, 3827, 4358, 4890, 5421,
    5953, 6484, 7016, 7547, 8079, 8610, 9142, 9673,
    10205, 10736, 11268, 11799, 12331, 12863, 13394, 13926,
    14457, 14989, 15520, 16052, 16583, 17115, 17646, 18178,
    18709, 19241, 19772, 20304, 20835, 21367, 21898, 22430,
    22961, 23493, 24024, 24556, 25087, 25619, 26150
};

/* round(2^32 * f0 / 8000), f0 = 50 + 350 * index / 128.
 * = round((128 + 7 * index) * 2^20 / 5). The remainder mod 5 is never 2.5.
 */
static uint32_t w0_from_index(int index)
{
    uint32_t n = (uint32_t)(128 + 7 * index) * 1048576u;

    return (n + 2u) / 5u;
}

/* Midpoint of two grid fundamentals, same rounding. */
static uint32_t w0_from_sum(int sum)
{
    uint32_t n = (uint32_t)(256 + 7 * sum) * 524288u;

    return (n + 2u) / 5u;
}

static int16_t startup_lsp_q1(int j, int hz)
{
    /* Nearest 0.5 Hz to the midpoint of j*4000/11 Hz and this frame's LSP.
     * round(2 * (j * 4000/11 + hz) / 2) = round((4000 * j + 11 * hz) / 11).
     */
    return (int16_t)((j * 4000 + hz * 11 + 5) / 11);
}

void c2fx_front_init(c2fx_front *st)
{
    st->ready = 0;
    st->prev_voiced = 0;
    st->prev_i = 0;
    st->prev_e = 0;
    for (int j = 0; j < C2FX_LSP_ORDER; j++) {
        /* Spec 4: previous LSP j = j * pi / 11 rad = j * 4000/11 Hz.
         * Stored as the nearest 0.5 Hz. It is used only for the first
         * sub-frame a; c2fx_dequant_frame recomputes that midpoint.
         */
        st->prev_lsp_q1[j] = (int16_t)((j * 8000 + 5) / 11);
    }
}

void c2fx_dequant_frame(c2fx_front *st, const c2fx_indices *idx, c2fx_subframe out[2])
{
    int16_t cur_q1[C2FX_LSP_ORDER];
    int hz = 0;
    int startup = !st->ready;

    for (int j = 0; j < C2FX_LSP_ORDER; j++) {
        hz += c2fx_lsp_tab[j][idx->d[j]];
        cur_q1[j] = (int16_t)(hz * 2);
    }

    out[0].voiced = idx->v_a;
    out[1].voiced = idx->v_b;

    out[1].w0 = w0_from_index(idx->i);
    out[1].pitch_key = idx->i;
    out[1].pitch_mid = 0;
    out[1].energy_log2_q11 = energy_log2_q11[idx->e * 2];
    for (int j = 0; j < C2FX_LSP_ORDER; j++) {
        out[1].lsp_q1[j] = cur_q1[j];
    }

    /* Energy midpoint in dB is linear in the index, so the log2 midpoint
     * is the table entry at the sum of the two indices.
     */
    out[0].energy_log2_q11 = energy_log2_q11[st->prev_e + idx->e];
    if (startup) {
        for (int j = 0; j < C2FX_LSP_ORDER; j++) {
            out[0].lsp_q1[j] = startup_lsp_q1(j, cur_q1[j] / 2);
        }
    } else {
        for (int j = 0; j < C2FX_LSP_ORDER; j++) {
            out[0].lsp_q1[j] = (int16_t)((st->prev_lsp_q1[j] + cur_q1[j]) / 2);
        }
    }

    /* Spec 4: average w0 only when the previous sub-frame b and this
     * sub-frame a are both voiced. The first frame has previous voiced 0.
     */
    if (!startup && st->prev_voiced && idx->v_a) {
        out[0].w0 = w0_from_sum(st->prev_i + idx->i);
        out[0].pitch_key = st->prev_i + idx->i;
        out[0].pitch_mid = 1;
    } else {
        out[0].w0 = out[1].w0;
        out[0].pitch_key = idx->i;
        out[0].pitch_mid = 0;
    }

    st->ready = 1;
    st->prev_voiced = (uint8_t)idx->v_b;
    st->prev_i = (uint8_t)idx->i;
    st->prev_e = (uint8_t)idx->e;
    for (int j = 0; j < C2FX_LSP_ORDER; j++) {
        st->prev_lsp_q1[j] = cur_q1[j];
    }
}

/* Midpoints between adjacent energy levels (spec 10.1.3), rounded
 * up to the Q24 log2 grid. Entry k is
 * ceil(((-10 + (k + 0.5) * 50/32) / 10) * log2(10) * 2^24).
 */
static const int32_t energy_midpoints_q24[31] = {
    -51378587, -42670352, -33962117, -25253882, -16545646,
    -7837411, 870824, 9579059, 18287294, 26995530,
    35703765, 44412000, 53120235, 61828470, 70536705,
    79244941, 87953176, 96661411, 105369646, 114077881,
    122786117, 131494352, 140202587, 148910822, 157619057,
    166327293, 175035528, 183743763, 192451998, 201160233,
    209868468,
};

int c2fx_quant_energy_index(const c2fx_enc_row *row)
{
    int lo = 0;
    int hi = 31;

    /* The first midpoint above this row's energy is its index.
     * Exactly on a midpoint, the model takes the higher index.
     */
    while (lo < hi) {
        int mid = (lo + hi) / 2;

        if (row->energy_log2_q24 < energy_midpoints_q24[mid]) {
            hi = mid;
        } else {
            lo = mid + 1;
        }
    }
    return lo;
}

void c2fx_quant_lsp_indices(const c2fx_enc_row *row, int d[C2FX_LSP_ORDER])
{
    int32_t sum = 0;

    for (int j = 0; j < C2FX_LSP_ORDER; j++) {
        const int16_t *tab = c2fx_lsp_tab[j];
        int32_t target = row->lsp_hz_q16[j] - sum * 65536;
        int k = 0;

        /* The table is increasing. Stop at the first midpoint at or
         * above the target, so a tie selects the lower entry.
         */
        while (k < 31 && target > ((int32_t)tab[k] + tab[k + 1]) * 32768) {
            k++;
        }
        d[j] = k;
        sum += tab[k];
    }
}

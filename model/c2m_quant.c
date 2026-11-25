// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The c2fx Authors

#include "c2m_quant.h"

#include "c2_3200_tab.h"

#include <math.h>

double c2m_f0_hz(int index)
{
    /* Spec 3.2. The divisor is 128; index 128 would be 400 Hz and is never used. */
    return 50.0 + 350.0 * (double)index / 128.0;
}

double c2m_w0_rad(int index)
{
    return 2.0 * C2M_PI * c2m_f0_hz(index) / C2M_FS_HZ;
}

void c2m_lsp_hz_to_rad(const double hz[C2M_LSP_ORDER], double rad[C2M_LSP_ORDER])
{
    for (int j = 0; j < C2M_LSP_ORDER; j++) {
        rad[j] = 2.0 * C2M_PI * hz[j] / C2M_FS_HZ;
    }
}

void c2m_lsp_accept(double lsp_hz[C2M_LSP_ORDER])
{
#if C2M_LSP_KEEP_RAW_SUM
    (void)lsp_hz;
#else
#error "C2M_LSP_KEEP_RAW_SUM is the only LSP range rule implemented"
#endif
}

void c2m_dequant(const c2m_indices *idx, c2m_params *out)
{
    double hz = 0.0;

    out->f0_hz = c2m_f0_hz(idx->i);
    out->w0 = c2m_w0_rad(idx->i);
    out->energy_db = C2M_ENERGY_STEP_DB * (double)idx->e;
    for (int j = 0; j < C2M_LSP_ORDER; j++) {
        hz += (double)c2fx_lsp_tab[j][idx->d[j]];
        out->lsp_hz[j] = hz;
    }
    c2m_lsp_accept(out->lsp_hz);
    c2m_lsp_hz_to_rad(out->lsp_hz, out->lsp_rad);
}

double c2m_energy_power(double rel_db)
{
    return pow(10.0, (C2M_E_MIN_DB + rel_db) / 10.0);
}

/* Round half away from zero for a non-negative raw index, then clip. */
static int clip_round(double raw, int max_index)
{
    int index;

    if (!(raw > -0.5)) {
        return 0;
    }
    if (raw >= (double)max_index + 0.5) {
        return max_index;
    }
    index = (int)floor(raw + 0.5);
    if (index < 0) {
        return 0;
    }
    if (index > max_index) {
        return max_index;
    }
    return index;
}

int c2m_quant_w0_index(double w0)
{
    double f0 = w0 * C2M_FS_HZ / (2.0 * C2M_PI);
    double raw = (f0 - 50.0) * 128.0 / 350.0;

    return clip_round(raw, 127);
}

int c2m_quant_energy_index(double energy)
{
    double raw;

    if (!(energy > 0.0)) {
        return 0;
    }
    raw = (10.0 * log10(energy) + 10.0) * 32.0 / 50.0;
    return clip_round(raw, 31);
}

void c2m_quant_lsp_indices(const double lsp_rad[C2M_LSP_ORDER], int d[C2M_LSP_ORDER])
{
    double q_hz = 0.0;

    for (int j = 0; j < C2M_LSP_ORDER; j++) {
        double target = lsp_rad[j] * C2M_FS_HZ / (2.0 * C2M_PI) - q_hz;
        int best = 0;
        double best_dist = fabs((double)c2fx_lsp_tab[j][0] - target);

        if (!(lsp_rad[j] == lsp_rad[j])) {
            best = 0;
            d[j] = 0;
            q_hz += (double)c2fx_lsp_tab[j][0];
            continue;
        }
        for (int k = 1; k < 32; k++) {
            double dist = fabs((double)c2fx_lsp_tab[j][k] - target);

            if (dist < best_dist) {
                best_dist = dist;
                best = k;
            }
        }
        d[j] = best;
        q_hz += (double)c2fx_lsp_tab[j][best];
    }
}

void c2m_quant_encode(
    int voiced_a,
    int voiced_b,
    double w0_b,
    double energy_b,
    const double lsp_rad_b[C2M_LSP_ORDER],
    c2m_indices *idx
)
{
    idx->v_a = voiced_a ? 1 : 0;
    idx->v_b = voiced_b ? 1 : 0;
    idx->i = c2m_quant_w0_index(w0_b);
    idx->e = c2m_quant_energy_index(energy_b);
    c2m_quant_lsp_indices(lsp_rad_b, idx->d);
}

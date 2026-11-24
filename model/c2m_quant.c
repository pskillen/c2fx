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

// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The c2fx Authors

#ifndef C2FX_TEST_LSD_HELPER_H
#define C2FX_TEST_LSD_HELPER_H

#include <stddef.h>
#include <stdint.h>

/* Reference decode is already several dB from the source; tests compare against
 * that distance plus this margin, not a fixed LSD ceiling. */
#define C2M_TEST_INPUT_LSD_MARGIN_DB 1.0

double c2m_test_mean_lsd(
    const int16_t *left,
    const int16_t *right,
    const int16_t *gate,
    long count,
    int *used_out
);

int c2m_test_decode_c2(const uint8_t *bits, size_t nbytes, int16_t **pcm_out, long *count_out);

#endif

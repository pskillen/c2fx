// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The c2fx Authors

/* Compile the streaming glue with a pitch probe to check both row centers. */
#define c2fx_pitch_hz probe_pitch
#define c2fx_enc_init probe_init
#define c2fx_encode probe_encode
#include "../src/c2fx_encode.c"
#undef c2fx_pitch_hz
#undef c2fx_enc_init
#undef c2fx_encode

#include <stdio.h>

int32_t c2fx_pitch_hz(const int16_t x[C2FX_PITCH_SPAN], int32_t prev_hz_q20,
                      struct c2fx_enc_scratch *scratch);

static c2fx_enc_t *observed;
static int calls;
static int failed;

int32_t probe_pitch(const int16_t x[C2FX_PITCH_SPAN], int32_t previous,
                    struct c2fx_enc_scratch *scratch)
{
    if (calls >= 2 || x != observed->pcm + calls * C2FX_HOP) {
        fprintf(stderr, "encoder: row %d pitch center differs\n", calls);
        failed = 1;
    }
    calls++;
    return c2fx_pitch_hz(x, previous, scratch);
}

int main(void)
{
    c2fx_enc_t state;
    c2fx_scratch_t scratch;
    int16_t pcm[C2FX_FRAME_LEN] = {0};
    uint8_t bits[C2FX_BITS_BYTES];
    observed = &state;
    probe_init(&state);
    probe_encode(&state, pcm, bits, &scratch);
    if (calls != 2) {
        fprintf(stderr, "encoder: expected two pitch rows, got %d\n", calls);
        return 1;
    }
    return failed;
}

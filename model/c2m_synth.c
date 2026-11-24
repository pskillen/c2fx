// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The c2fx Authors

#include "c2m_synth.h"

#include "c2m_lpc.h"

#include <math.h>
#include <stddef.h>

/* int16 saturation. The positive clip observed on speech is +32767. */
#define C2M_SAMPLE_MAX 32767.0
#define C2M_SAMPLE_MIN (-32768.0)

void c2m_ola_hop(double ola[C2M_HOP], const double block[C2M_FRAME_SAMPLES], double out[C2M_HOP])
{
    for (int n = 0; n < C2M_HOP; n++) {
        double rise = (double)n / (double)C2M_HOP;
        double fall = 2.0 - (double)(n + C2M_HOP) / (double)C2M_HOP;

        out[n] = ola[n] + rise * block[n];
        ola[n] = fall * block[n + C2M_HOP];
    }
}

static void synth_block(c2m_decode *st, const c2m_subframe *sub, double block[C2M_FRAME_SAMPLES])
{
    double a[C2M_LPC_ORDER + 1];
    c2m_envelope env;
    double minphase[C2M_SPECTRUM_BINS + 1];
    double y[C2M_DFT_SIZE];
    double gain;
    double peak = 0.0;

    c2m_lsp_to_lpc(sub->lsp_rad, a);
    c2m_envelope_sample(a, sub->w0, sub->energy_db, &env);
    c2m_min_phase(env.power, minphase);
    st->phi1 = c2m_advance_excitation(st->phi1, sub->w0, sub->voiced);
    /* Peak of one harmonic is A_m * 10^(gain/20). That is the spec 6.3
     * sinusoid peak; the gain already includes the one-sided factor of about 2.
     */
    gain = pow(10.0, C2M_SYNTH_GAIN_DB / 20.0);
    for (int m = 0; m < env.count; m++) {
        if (env.amp[m] > peak) {
            peak = env.amp[m];
        }
    }
    for (int n = 0; n < C2M_DFT_SIZE; n++) {
        y[n] = 0.0;
    }
    for (int m = 1; m <= env.count; m++) {
        double bin_f = (double)m * sub->w0 * (double)C2M_DFT_SIZE / (2.0 * C2M_PI);
        int bin = (int)floor(bin_f + 0.5);
        int random_phase = !sub->voiced;
        double exc;
        double theta;
        double amp;
        double omega;
        double c;
        double s;
        double wc;
        double ws;

        if (!random_phase && st->noise_floor != NULL &&
            st->noise_floor(m, env.amp[m - 1], peak, st->noise_floor_user) != 0) {
            random_phase = 1;
        }
        if (random_phase) {
            exc = c2m_random_phase(&st->rng);
        } else {
            exc = (double)m * st->phi1;
        }
        if (bin < 1 || bin > C2M_SPECTRUM_BINS) {
            continue;
        }
        theta = exc + minphase[bin];
        amp = env.amp[m - 1] * gain;
        omega = 2.0 * C2M_PI * (double)bin / (double)C2M_DFT_SIZE;
        c = cos(theta);
        s = sin(theta);
        wc = cos(omega);
        ws = sin(omega);
        for (int n = 0; n < C2M_DFT_SIZE; n++) {
            double next_c = c * wc - s * ws;

            y[n] += amp * c;
            s = c * ws + s * wc;
            c = next_c;
        }
    }
    /* Block centred on index 0: rising half from the end, falling half from the start. */
    for (int n = 0; n < C2M_HOP; n++) {
        block[n] = y[C2M_DFT_SIZE - C2M_HOP + n];
        block[C2M_HOP + n] = y[n];
    }
}

static int16_t sat_round(double x)
{
    if (x > C2M_SAMPLE_MAX) {
        x = C2M_SAMPLE_MAX;
    } else if (x < C2M_SAMPLE_MIN) {
        x = C2M_SAMPLE_MIN;
    }
    return (int16_t)round(x);
}

static void write_silence(c2m_decode *st, int16_t out[C2M_FRAME_SAMPLES])
{
    for (int n = 0; n < C2M_FRAME_SAMPLES; n++) {
        out[n] = 0;
    }
    for (int n = 0; n < C2M_HOP; n++) {
        st->ola[n] = 0.0;
    }
}

/* After the repeat limit, the next good frame fades in on the start-up path. */
static void restore_startup(c2m_decode *st)
{
    uint32_t rng = st->rng;

    c2m_decode_init(st);
    st->rng = rng;
}

static const c2m_indices *lost_frame(c2m_decode *st, c2m_indices *rep)
{
    int energy;

    st->lost_run += 1;
    if (!st->has_prev || st->lost_run > C2M_LOST_MAX_FRAMES) {
        return NULL;
    }
    energy = st->prev_e - C2M_LOST_ENERGY_STEPS;
    if (energy < 0) {
        energy = 0;
    }
    rep->v_a = st->prev_voiced;
    rep->v_b = st->prev_voiced;
    rep->i = st->prev_i;
    rep->e = energy;
    for (int j = 0; j < C2M_LSP_ORDER; j++) {
        rep->d[j] = st->prev_d[j];
    }
    return rep;
}

void c2m_decode_pcm(c2m_decode *st, const c2m_indices *idx, int16_t out[C2M_FRAME_SAMPLES])
{
    c2m_subframe sub[2];
    c2m_indices repeated;
    double block[C2M_FRAME_SAMPLES];
    double hop[C2M_HOP];

    if (idx == NULL) {
        idx = lost_frame(st, &repeated);
        if (idx == NULL) {
            write_silence(st, out);
            restore_startup(st);
            return;
        }
    } else {
        st->lost_run = 0;
    }

    c2m_decode_frame(st, idx, sub);
    for (int s = 0; s < 2; s++) {
        synth_block(st, &sub[s], block);
        c2m_ola_hop(st->ola, block, hop);
        for (int n = 0; n < C2M_HOP; n++) {
            out[s * C2M_HOP + n] = sat_round(hop[n]);
        }
    }
}

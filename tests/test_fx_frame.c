// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The c2fx Authors
// Fixed-point frame unpack against the float model's c2m_frame.

#include "c2fx_internal.h"
#include "c2m_frame.h"

#include <dirent.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define MAX_SETS 32
#define NAME_LEN 64

static uint32_t xorshift32(uint32_t *state)
{
    uint32_t x = *state;

    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static int same(const c2m_indices *m, const c2fx_indices *f)
{
    if (m->v_a != f->v_a || m->v_b != f->v_b || m->i != f->i || m->e != f->e) {
        return 0;
    }
    for (int j = 0; j < C2FX_LSP_ORDER; j++) {
        if (m->d[j] != f->d[j]) {
            return 0;
        }
    }
    return 1;
}

static int test_random(void)
{
    uint32_t state = 0xC2F064u;

    for (int n = 0; n < 100000; n++) {
        uint8_t bytes[8];
        c2m_indices m;
        c2fx_indices f;

        for (int i = 0; i < 8; i++) {
            bytes[i] = (uint8_t)xorshift32(&state);
        }
        c2m_frame_unpack(bytes, &m);
        c2fx_frame_unpack(bytes, &f);
        if (!same(&m, &f)) {
            fprintf(stderr, "frame unpack mismatch on random frame %d\n", n);
            return 1;
        }
    }
    printf("frame unpack: 100000 random frames match\n");
    return 0;
}

static int ends_with(const char *name, const char *suffix)
{
    size_t n = strlen(name);
    size_t s = strlen(suffix);

    return n >= s && memcmp(name + n - s, suffix, s) == 0;
}

static int name_less(const void *a, const void *b)
{
    return strcmp((const char *)a, (const char *)b);
}

static int test_vectors(void)
{
    DIR *dp = opendir("vectors");
    struct dirent *ent;
    char names[MAX_SETS][NAME_LEN];
    int count = 0;

    if (dp == NULL) {
        fprintf(stderr, "cannot open vectors (run from the repository root)\n");
        return 1;
    }
    while ((ent = readdir(dp)) != NULL) {
        if (!ends_with(ent->d_name, ".c2")) {
            continue;
        }
        if (count >= MAX_SETS || strlen(ent->d_name) >= NAME_LEN) {
            fprintf(stderr, "too many bitstreams in vectors\n");
            closedir(dp);
            return 1;
        }
        memcpy(names[count], ent->d_name, strlen(ent->d_name) + 1);
        count += 1;
    }
    closedir(dp);
    if (count == 0) {
        fprintf(stderr, "no .c2 files in vectors\n");
        return 1;
    }
    qsort(names, (size_t)count, NAME_LEN, name_less);
    for (int s = 0; s < count; s++) {
        char path[128];
        FILE *fp;
        int frame = 0;

        snprintf(path, sizeof path, "vectors/%s", names[s]);
        fp = fopen(path, "rb");
        if (fp == NULL) {
            fprintf(stderr, "cannot read %s\n", path);
            return 1;
        }
        for (;;) {
            uint8_t bytes[8];
            c2m_indices m;
            c2fx_indices f;
            size_t n = fread(bytes, 1, 8, fp);

            if (n == 0) {
                break;
            }
            if (n != 8) {
                fprintf(stderr, "%s: trailing %zu bytes\n", names[s], n);
                fclose(fp);
                return 1;
            }
            c2m_frame_unpack(bytes, &m);
            c2fx_frame_unpack(bytes, &f);
            if (!same(&m, &f)) {
                fprintf(stderr, "frame unpack mismatch in %s frame %d\n", names[s], frame);
                fclose(fp);
                return 1;
            }
            frame += 1;
        }
        fclose(fp);
        printf("frame unpack: %s %d frames match\n", names[s], frame);
    }
    return 0;
}

int main(void)
{
    if (test_random() != 0 || test_vectors() != 0) {
        return 1;
    }
    return 0;
}

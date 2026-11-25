// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The c2fx Authors

#include "c2fx.h"

#ifndef C2FX_VERSION
#define C2FX_VERSION "local"
#endif

const char *c2fx_version(void)
{
    return C2FX_VERSION;
}

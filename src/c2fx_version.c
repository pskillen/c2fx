// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The c2fx Authors

#include "c2fx.h"

#define C2FX_STR_IMPL(x) #x
#define C2FX_STR(x) C2FX_STR_IMPL(x)

const char *c2fx_version(void)
{
    return C2FX_STR(C2FX_VERSION_MAJOR) "."
           C2FX_STR(C2FX_VERSION_MINOR) "."
           C2FX_STR(C2FX_VERSION_PATCH);
}

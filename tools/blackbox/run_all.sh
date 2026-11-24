#!/bin/sh
# SPDX-License-Identifier: MIT
# Copyright (c) The c2fx Authors
# Run every experiment; each prints its findings. Usage: C2FX_BLACKBOX=... C2FX_WORK=... ./run_all.sh [python]
# Output goes to stdout; redirect or tee as you like. Generated data stays in $C2FX_WORK (default ./work).
PY="${1:-python3}"
cd "$(dirname "$0")" || exit 1
for x in x0*.py; do
  echo "=== $x"
  "$PY" "$x" || echo "FAILED: $x"
done

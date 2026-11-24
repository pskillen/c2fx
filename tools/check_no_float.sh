#!/bin/sh
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 The c2fx Authors

# check_no_float.sh <objdir> <prefix>
# Fails when any object has an undefined symbol not defined elsewhere in
# <objdir> and not listed in tools/allowed_undefined.txt.
# <prefix> is the toolchain prefix, including the trailing hyphen, so nm is
# ${prefix}nm.

set -eu
export LC_ALL=C

if [ "$#" -ne 2 ]; then
    echo "usage: tools/check_no_float.sh <objdir> <prefix>" >&2
    exit 2
fi

objdir=$1
prefix=$2
nm=${prefix}nm
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
allowed=$script_dir/allowed_undefined.txt

if [ ! -d "$objdir" ]; then
    echo "check_no_float: not a directory: $objdir" >&2
    exit 2
fi
if [ ! -x "$nm" ]; then
    echo "check_no_float: nm not executable: $nm" >&2
    exit 2
fi
if [ ! -f "$allowed" ]; then
    echo "check_no_float: missing $allowed" >&2
    exit 2
fi

list=$(mktemp)
defined=$(mktemp)
undef=$(mktemp)
hits=$(mktemp)
trap 'rm -f "$list" "$defined" "$undef" "$hits"' EXIT

find "$objdir" -type f -name '*.o' | sort > "$list"
if [ ! -s "$list" ]; then
    echo "check_no_float: no objects in $objdir" >&2
    exit 1
fi

count=0
while IFS= read -r obj; do
    count=$((count + 1))
    "$nm" "$obj" | awk 'NF >= 3 && $2 != "U" {
        sym = $NF
        sub(/@.*/, "", sym)
        print sym
    }' >> "$defined"
done < "$list"
sort -u "$defined" -o "$defined"

while IFS= read -r obj; do
    "$nm" -u "$obj" | awk '{ sym = $NF; sub(/@.*/, "", sym); print sym }' > "$undef"
    while IFS= read -r sym; do
        if grep -Fqx "$sym" "$defined"; then
            continue
        fi
        if grep -Fqx "$sym" "$allowed"; then
            continue
        fi
        printf '%s: %s\n' "$obj" "$sym" >> "$hits"
    done < "$undef"
done < "$list"

if [ -s "$hits" ]; then
    echo "check_no_float: forbidden symbol"
    sort -u "$hits"
    exit 1
fi

echo "check_no_float: ok (${count} object(s))"
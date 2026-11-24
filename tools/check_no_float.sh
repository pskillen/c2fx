#!/bin/sh
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 The c2fx Authors

# check_no_float.sh <objdir> <prefix>
# Fails when any object references a soft-float helper, libm, or the heap.
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

if [ ! -d "$objdir" ]; then
    echo "check_no_float: not a directory: $objdir" >&2
    exit 2
fi
if [ ! -x "$nm" ]; then
    echo "check_no_float: nm not executable: $nm" >&2
    exit 2
fi

list=$(mktemp)
hits=$(mktemp)
nmout=$(mktemp)
trap 'rm -f "$list" "$hits" "$nmout"' EXIT

find "$objdir" -type f -name '*.o' | sort > "$list"
if [ ! -s "$list" ]; then
    echo "check_no_float: no objects in $objdir" >&2
    exit 1
fi

count=0
while IFS= read -r obj; do
    count=$((count + 1))
    "$nm" "$obj" > "$nmout"
    awk 'NF { print $NF }' "$nmout" | while IFS= read -r sym; do
        sym=${sym%%@*}
        case $sym in
            __aeabi_f*|__aeabi_d*|__*sf3|__*df3|\
                malloc|free|calloc|realloc|sin|cos|sqrt|exp|log|pow)
                printf '%s: %s\n' "$obj" "$sym" >> "$hits"
                ;;
        esac
    done
done < "$list"

if [ -s "$hits" ]; then
    echo "check_no_float: forbidden symbol"
    cat "$hits"
    exit 1
fi

echo "check_no_float: ok (${count} object(s))"

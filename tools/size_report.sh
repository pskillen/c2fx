#!/bin/sh
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 The c2fx Authors

# size_report.sh <target> [size-tool]
# Prints text+data for each object in build/<target> and exits non-zero
# when that sum, or a listed decoder sum, is over tools/budgets.txt.
# size-tool defaults to size (pass arm-none-eabi-size or csky-elf-size in CI).

set -eu
export LC_ALL=C

if [ "$#" -lt 1 ] || [ "$#" -gt 2 ]; then
    echo "usage: tools/size_report.sh <target> [size-tool]" >&2
    exit 2
fi

target=$1
size_tool=${2:-size}
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root=$(CDPATH= cd -- "$script_dir/.." && pwd)
budgets=$script_dir/budgets.txt
objdir=$root/build/$target

if [ ! -f "$budgets" ]; then
    echo "size_report: missing $budgets" >&2
    exit 2
fi

# Print the value for key, or return 1 when the key is absent.
budget_get() {
    key=$1
    awk -F= -v key="$key" '
        /^[[:space:]]*#/ { next }
        /^[[:space:]]*$/ { next }
        {
            k = $1
            gsub(/^[[:space:]]+|[[:space:]]+$/, "", k)
            if (k == key) {
                v = substr($0, index($0, "=") + 1)
                gsub(/^[[:space:]]+|[[:space:]]+$/, "", v)
                print v
                found = 1
                exit
            }
        }
        END { if (!found) exit 1 }
    ' "$budgets"
}

object_bytes() {
    "$size_tool" "$1" | awk 'NR == 2 { printf "%d\n", $1 + $2 }'
}

if ! total_budget=$(budget_get "${target}.total"); then
    echo "size_report: no ${target}.total in $budgets" >&2
    exit 2
fi

radio=$(budget_get "${target}.radio" || true)
decoder_budget=$(budget_get "${target}.decoder" || true)
decoder_objs=$(budget_get "${target}.decoder_objs" || true)
encoder_objs=$(budget_get "${target}.encoder_objs" || true)

if [ ! -d "$objdir" ]; then
    echo "size_report: no object directory $objdir" >&2
    exit 1
fi

list=$(mktemp)
trap 'rm -f "$list"' EXIT
find "$objdir" -type f -name '*.o' | sort > "$list"
if [ ! -s "$list" ]; then
    echo "size_report: no objects in $objdir" >&2
    exit 1
fi

if [ -n "$radio" ]; then
    printf '%s %s\n' "$radio" "$target"
else
    printf '%s\n' "$target"
fi

total_bytes=0
while IFS= read -r obj; do
    bytes=$(object_bytes "$obj")
    rel=${obj#"$root"/}
    printf '%8d  %s\n' "$bytes" "$rel"
    total_bytes=$((total_bytes + bytes))
done < "$list"

status=0
printf '%8d  total (budget %s)\n' "$total_bytes" "$total_budget"
if [ "$total_bytes" -gt "$total_budget" ]; then
    echo "size_report: total ${total_bytes} exceeds budget ${total_budget}"
    status=1
fi

sum_named() {
    names=$1
    sum=0
    for name in $names; do
        path=$objdir/$name
        if [ ! -f "$path" ]; then
            echo "size_report: listed object missing: $name" >&2
            return 1
        fi
        bytes=$(object_bytes "$path")
        sum=$((sum + bytes))
    done
    printf '%s\n' "$sum"
}

if [ -n "$decoder_budget" ]; then
    if ! decoder_bytes=$(sum_named "$decoder_objs"); then
        exit 1
    fi
    if [ -n "$decoder_objs" ]; then
        printf '%8d  decoder (budget %s)\n' "$decoder_bytes" "$decoder_budget"
    else
        printf '%8d  decoder (budget %s, no objects listed)\n' "$decoder_bytes" "$decoder_budget"
    fi
    if [ "$decoder_bytes" -gt "$decoder_budget" ]; then
        echo "size_report: decoder ${decoder_bytes} exceeds budget ${decoder_budget}"
        status=1
    fi
fi

if [ -n "$encoder_objs" ]; then
    if ! encoder_bytes=$(sum_named "$encoder_objs"); then
        exit 1
    fi
    printf '%8d  encoder (no separate budget)\n' "$encoder_bytes"
else
    echo "       0  encoder (no objects listed)"
fi

if [ "$status" -eq 0 ]; then
    echo "size_report: within budget"
fi
exit "$status"

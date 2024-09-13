#!/usr/bin/env bash
set -eou pipefail

#usage: binary-header.sh filename binary_dir

in_file="$1"
binary_dir="$2"

if ! [ -f "$in_file" ]; then
  echo "File $in_file does not exist"
  exit 1
fi

if ! [ -d "$binary_dir" ]; then
  echo "Binary directory $binary_dir does not exist"
  exit 1
fi

if [ $# -gt 2 ]; then
  root_dir="$3"
else
  script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &> /dev/null && pwd)"
  root_dir="${script_dir%/}/../src"
fi

path="$(realpath --relative-to="$root_dir" "$in_file")"
out_h="${binary_dir%/}/$(basename "$in_file").h++"
out_S="${binary_dir%/}/$(basename "$in_file").S"
sym="$(basename "$in_file" | sed -E -e 's/[^[:alnum:]]+/_/g' -e 's/^_+|_+$//g')"

mkdir -p "$(dirname "$out_h")"

cat > "$out_h" <<END
#pragma once
#include <stdlib.h>
#include <string_view>

extern const char $sym[];
extern const int ${sym}_size;
static inline std::string_view ${sym}_str() { return { $sym, (size_t)${sym}_size }; }
END

cat > "$out_S" <<END
.section .note.GNU-stack,"",@progbits
    .global $sym
    .global ${sym}_size
    .section .rodata
$sym:
    .incbin "$path"
1:
${sym}_size:
    .int 1b - $sym
END

#!/bin/sh
set -eu
# With a second directory only compares (used by the repro_check_mismatch test).
[ "$#" -eq 1 ] || [ "$#" -eq 2 ] || { echo 'usage: repro_check.sh <build> [<second-build>]' >&2; exit 2; }
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
first=$(realpath "$1")
second=${2:-$root/build/port/web-repro}
if [ "$first" = "$second" ] || [ "$first" -ef "$second" ]; then
  echo 'input must differ from the second build' >&2
  exit 2
fi
if [ "$#" -eq 1 ]; then
  cd "$root"
  rm -rf -- "$second"
  # The build directory names its preset (web or web-release), so the served build is the one rebuilt.
  cmake --preset "$(basename "$first")" -B "$second"
  cmake --build "$second" --target dw1 dw1_pack web_sw
fi
for file in dw1.wasm dw1_pack.wasm; do
  a=$(sha256sum "$first/$file"); a=${a%% *}
  b=$(sha256sum "$second/$file"); b=${b%% *}
  echo "$file: $a $b"
  [ "$a" = "$b" ] || { echo "$file: reproducibility mismatch" >&2; exit 1; }
done

#!/bin/sh
# Class B (needs the user's disc in DW1_DISC_DIR, else skip = 77): the Manifest of the USA disc is
# tools/port/manifest/us.tsv, two builds give the same Pack, and it verifies. The Pack is left at
# <out> (a CTest fixture for dw1_overlay; never commit it: it holds the disc's bytes).
# Usage: test_pack_disc.sh <dw1-pack> <repo> <out>
set -eu
[ -n "${DW1_DISC_DIR:-}" ] || { echo "pack_disc: DW1_DISC_DIR not set, skipped"; exit 77; }
pack=$1
tsv=$2/tools/port/manifest/us.tsv
out=$3
img=$(ls "$DW1_DISC_DIR"/*.cue 2>/dev/null | head -n 1)
[ -n "$img" ] || { echo "pack_disc: no .cue in $DW1_DISC_DIR" >&2; exit 1; }
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

"$pack" manifest "$img" "$tmp/us.tsv"
cmp "$tmp/us.tsv" "$tsv"
test "$(grep -vc '^#' "$tsv")" = 819
test "$(grep -c '^@exe	' "$tsv")" = 1
"$pack" build "$img" "$tsv" "$tmp/us.pack"
a=$(sha256sum < "$tmp/us.pack")
"$pack" build "$img" "$tsv" "$tmp/us.pack"
test "$(sha256sum < "$tmp/us.pack")" = "$a"
test "${a%% *}" = c01e5c3680ffdf50d0342f16ec978c6d107a33529688dae6a817bd63947f0cf6
"$pack" verify "$tmp/us.pack" "$tsv"
mv "$tmp/us.pack" "$out"
echo "pack_disc: OK, Pack sha256 ${a%% *}"

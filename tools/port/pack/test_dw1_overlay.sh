#!/bin/sh
# Class B (needs the user's disc in DW1_DISC_DIR, else skip = 77): dw1 boots on the USA Pack left
# by pack_disc until the first overlay load. One trace line "overlay": the overlay restored once,
# its bytes at the overlay's <ovl>_START with the SHA-256 of tools/port/manifest/us.tsv.
# Usage: test_dw1_overlay.sh <dw1> <repo> <build dir>
set -eu
[ -n "${DW1_DISC_DIR:-}" ] || { echo "dw1_overlay: DW1_DISC_DIR not set, skipped"; exit 77; }
dw1=$1
tsv=$2/tools/port/manifest/us.tsv
build=$3
cd "$build"
rm -f dw1_overlay_trace.log
# --frames bounds the run if overlay detection regresses (the first overlay loads before frame 3).
DW1_TRACE=dw1_overlay_trace.log "$dw1" --headless --pack us.pack --until-overlay --frames 300 \
	> dw1_overlay.out 2>&1 || { cat dw1_overlay.out; echo "dw1_overlay: dw1 failed" >&2; exit 1; }
cat dw1_overlay.out
if grep -qE 'ERROR: AddressSanitizer|runtime error' dw1_overlay.out; then
	echo "dw1_overlay: sanitizer error" >&2; exit 1
fi
grep -q 'overlay loaded' dw1_overlay.out ||
	{ echo "dw1_overlay: dw1 ended without loading an overlay" >&2; exit 1; }
test "$(grep -c ' overlay ' dw1_overlay_trace.log)" = 1
# "<cycle> overlay <lib> <path> dest=0x<addr> sha256=<hex>"
set -- $(grep ' overlay ' dw1_overlay_trace.log)
path=$4 dest=${5#dest=} sha=${6#sha256=}
want_sha=$(p="$path" awk -F'\t' '$1 == ENVIRON["p"] { print $6 }' "$tsv")
test -n "$want_sha" && test "$sha" = "$want_sha" ||
	{ echo "dw1_overlay: $path sha256 $sha, Manifest $want_sha" >&2; exit 1; }
# \MOV_REL.BIN;1 -> mov_START in the generated header (several overlays share a start).
ovl=$(printf %s "$path" | sed 's/^\\//; s/_REL\.BIN;1$//' | tr 'A-Z' 'a-z')
start=$(sed -n "s/^#define ${ovl}_START ((char \*)PS1_RAM_ADDR(\(0x[0-9A-Fa-f]*\)u))$/\1/p" \
	generated/dw1_overlay_starts.h)
test -n "$start" && test "$(printf '%u' "$dest")" = "$(printf '%u' "$start")" ||
	{ echo "dw1_overlay: $path at $dest, ${ovl}_START $start" >&2; exit 1; }
echo "dw1_overlay: OK, $path at $dest sha256 $sha"

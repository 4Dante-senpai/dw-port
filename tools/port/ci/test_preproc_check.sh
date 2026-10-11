#!/bin/sh
# Tests preproc_check.sh on temporary copies of the tree: unchanged, an unguarded change and a
# change behind #ifdef DW1_PORT.
set -eu
repo=$(cd "$(dirname "$0")/../../.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
check=$repo/tools/port/ci/preproc_check.sh
fail=0

copy() {
	mkdir "$tmp/$1"
	(cd "$repo" && tar -c src include text mk config) | tar -x -C "$tmp/$1"
}

copy same
if ! sh "$check" "$tmp/same" >"$tmp/out" 2>&1; then
	echo "FAIL unchanged tree reported a difference"; cat "$tmp/out"; fail=1
fi

copy unguarded
printf '\nint port_injected_global;\n' >>"$tmp/unguarded/src/main/sjis.c"
if sh "$check" "$tmp/unguarded" >"$tmp/out" 2>&1; then
	echo "FAIL unguarded change not detected"; fail=1
elif ! grep -q "src/main/sjis.c differs" "$tmp/out"; then
	echo "FAIL unguarded change not named"; cat "$tmp/out"; fail=1
fi

copy guarded
printf '\n#ifdef DW1_PORT\nint port_injected_global;\n#endif\n' >>"$tmp/guarded/include/dw/main.h"
if ! sh "$check" "$tmp/guarded" >"$tmp/out" 2>&1; then
	echo "FAIL guarded change reported a difference"; cat "$tmp/out"; fail=1
fi

copy mwcc
# Unguarded edit inside the __MWERKS__ branch of a header: only the mwcc pass can see it.
sed -i 's/^typedef char int8_t;$/typedef signed char int8_t;/' "$tmp/mwcc/include/dw/types.h"
grep -q '^typedef signed char int8_t;$' "$tmp/mwcc/include/dw/types.h"
if sh "$check" "$tmp/mwcc" >"$tmp/out" 2>&1; then
	echo "FAIL mwcc-only change not detected"; fail=1
elif ! grep -q "differs from upstream without DW1_PORT (mwcc)" "$tmp/out"; then
	echo "FAIL mwcc-only change not reported for mwcc"; cat "$tmp/out"; fail=1
fi

copy else_branch
# The #else branch of a DW1_PORT block is the PS1 build: changing it must be detected.
printf '\n#ifdef DW1_PORT\n#else\nint port_injected_global;\n#endif\n' >>"$tmp/else_branch/src/main/sjis.c"
if sh "$check" "$tmp/else_branch" >"$tmp/out" 2>&1; then
	echo "FAIL change in the #else branch not detected"; fail=1
fi

copy removed
# Dropping a TU from the build list must be detected.
sed -i '/src\/main\/sjis.c/d' "$tmp/removed/mk/version/us.mk"
if sh "$check" "$tmp/removed" >"$tmp/out" 2>&1; then
	echo "FAIL removed TU not detected"; fail=1
elif ! grep -q "TU list differs" "$tmp/out"; then
	echo "FAIL removed TU not reported"; cat "$tmp/out"; fail=1
fi

[ "$fail" = 0 ] && echo "test_preproc_check: OK"
exit "$fail"

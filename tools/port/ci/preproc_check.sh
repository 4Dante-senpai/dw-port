#!/bin/sh
# L0a (AD-14, NFR-6): every US game TU, preprocessed without DW1_PORT, must be identical in the
# tree under test and at the merge-base with upstream/main, in both the gcc and the mwcc
# (matching) configuration. Also checks that the source inventories are up to date.
# Usage: preproc_check.sh [TREE] [BASE]   (TREE defaults to the repo, BASE to the merge-base)
set -eu
repo=$(cd "$(dirname "$0")/../../.." && pwd)
tree=$(cd "${1:-$repo}" && pwd)
if [ $# -ge 2 ]; then
	base=$2
elif ! base=$(git -C "$repo" merge-base HEAD upstream/main 2>/dev/null); then
	echo "preproc_check: no upstream/main ref; run 'git fetch upstream' first" >&2
	exit 1
fi
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

mkdir "$tmp/base"
git -C "$repo" archive "$base" src include text mk config | tar -x -C "$tmp/base"

# Text headers of each side, generated as mk/text.mk does.
gen_text() {
	(cd "$1" && find text -mindepth 2 -name '*.yaml') | while read -r y; do
		h=$2/${y%.yaml}.h
		mkdir -p "$(dirname "$h")"
		python3 -I -B "$repo/tools/gen_text.py" us "$1/$y" "$h"
	done
}
gen_text "$tree" "$tmp/gen_tree"
gen_text "$tmp/base" "$tmp/gen_base"

# Preprocess as the upstream build does (TOOLCHAIN=gcc, or mwcc with __MWERKS__), without line
# markers or blank lines, so a #ifdef DW1_PORT block that expands to nothing leaves no trace.
# A preprocessor error stops the script (set -e) instead of comparing partial output.
pp() { # tree gen_dir file toolchain out
	if [ "$4" = mwcc ]; then defs="-U__GNUC__ -D__MWERKS__"; else defs=; fi
	# shellcheck disable=SC2086
	clang -E -P -w -m32 -std=gnu99 $defs -DVERSION_US -DLANGUAGE_C -I"$1/include/$4" \
		-I"$repo/external/psyq_headers/mw_lib41/include" -I"$1/include" -I"$2" \
		-o "$tmp/raw" "$1/$3"
	grep -v '^[[:space:]]*$' "$tmp/raw" >"$5" || true
}

python3 -I -B "$repo/tools/port/units/tu_list.py" --root "$tree" us >"$tmp/tus_tree"
python3 -I -B "$repo/tools/port/units/tu_list.py" --root "$tmp/base" us >"$tmp/tus_base"
bad=0
if ! cmp -s "$tmp/tus_tree" "$tmp/tus_base"; then
	echo "preproc_check: the US TU list differs from upstream:"
	diff "$tmp/tus_base" "$tmp/tus_tree" || true
	bad=$((bad + 1))
fi

n=0
for f in $(cut -f1 "$tmp/tus_tree"); do
	n=$((n + 1))
	if [ ! -f "$tmp/base/$f" ]; then
		echo "preproc_check: $f is not in upstream ($base)"
		bad=$((bad + 1))
		continue
	fi
	for tc in gcc mwcc; do
		pp "$tree" "$tmp/gen_tree" "$f" $tc "$tmp/a"
		pp "$tmp/base" "$tmp/gen_base" "$f" $tc "$tmp/b"
		if ! cmp -s "$tmp/a" "$tmp/b"; then
			echo "preproc_check: $f differs from upstream without DW1_PORT ($tc)"
			bad=$((bad + 1))
		fi
	done
done
if [ "$n" -lt 100 ]; then
	echo "preproc_check: only $n TUs listed; the TU list is broken"
	bad=$((bad + 1))
fi
echo "preproc_check: tus=$n configs=gcc,mwcc differ=$bad base=$base"

if [ "$tree" = "$repo" ]; then
	# The port never edits the splat config or the upstream make files.
	if ! git -C "$repo" diff --quiet "$base" -- config mk Makefile; then
		echo "preproc_check: config/, mk/ or Makefile differ from upstream:"
		git -C "$repo" diff --stat "$base" -- config mk Makefile
		bad=$((bad + 1))
	fi
	# A changed .c that is not a US TU is not preprocessed above: refuse it.
	for f in $(git -C "$repo" diff --name-only "$base" -- 'src/*.c'); do
		if ! grep -q "^$f	" "$tmp/tus_tree"; then
			echo "preproc_check: $f changed but is not a US TU, so L0a cannot check it"
			bad=$((bad + 1))
		fi
	done
	python3 -I -B "$repo/tools/port/metrics/ps1_literals.py" --check
	python3 -I -B "$repo/tools/port/metrics/isr_spins.py" --check --no-pending
fi
[ "$bad" = 0 ]

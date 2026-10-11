#!/bin/sh
# Compiles every game C file of VERSION (default us) to a wasm32 object with emcc, unchanged and
# with the upstream PsyQ headers, and prints the files that fail with their error counts.
# Run in the port image after `make regenerate`:
#   tools/port/docker_run.sh tools/port/metrics/wasm_compile_check.sh [VERSION]
VERSION=${1:-us}
FILES=$(make -s -p -n VERSION=$VERSION TOOLCHAIN=gcc 2>/dev/null | sed -n 's/^[A-Z0-9_]*_C_SRC := //p' |
	tr ' ' '\n' | grep '\.c$' | sort -u)
ok=0; bad=0; errors=0
for f in $FILES; do
	n=$(emcc -c -O1 -o /dev/null -std=gnu99 -funsigned-char -w -DVERSION_$(echo $VERSION | tr a-z A-Z) \
		-DLANGUAGE_C -Iinclude/gcc -Iexternal/psyq_headers/mw_lib41/include -Iinclude \
		-Ibuild/$VERSION/psx/generated "$f" 2>&1 | grep -c ' error: ')
	if [ "$n" = 0 ]; then ok=$((ok + 1)); else bad=$((bad + 1)); errors=$((errors + n)); echo "$n $f"; fi
done
echo "files_ok=$ok files_with_errors=$bad errors=$errors"

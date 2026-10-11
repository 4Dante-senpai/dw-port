#!/usr/bin/env bash
# AD-6 spike: per-overlay section reset with wasm-ld and lld (native i686). Run inside dw-port.
# Expected: OK for every config except wasm with -flto (LTO drops the section attribute).
set -u
cd "$(dirname "$0")"
out=$(mktemp -d)
rc=0
for opt in -O0 -O2; do
    emcc $opt -c ovl_wrap.c -o "$out/w.o" && emcc $opt "$out/w.o" main.c -o "$out/t.js" &&
        node "$out/t.js" | sed "s/^/wasm $opt: /" || rc=1
    if printf "#include <stdio.h>\n" | clang -m32 -x c - -c -o /dev/null 2>/dev/null; then
        clang -m32 $opt -c ovl_wrap.c -o "$out/n.o" && clang -m32 $opt -fuse-ld=lld "$out/n.o" main.c -o "$out/t" &&
            "$out/t" | sed "s/^/native-i686 $opt: /" || rc=1
    else
        echo "native-i686 $opt: SKIP (no gcc-multilib)"
    fi
done
rm -rf "$out"
exit $rc

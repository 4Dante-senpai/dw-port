#!/bin/sh
# The pre-commit hook rejects staged incbin directives under port/ and tools/port/ and accepts
# clean files. Usage: test_pre_commit.sh <repo>
set -eu
hook=$1/tools/port/hooks/pre-commit
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
git init -q "$tmp"
cd "$tmp"
mkdir -p port tools/port
run() { git add -A && bash "$hook" >/dev/null 2>&1; }
check() { # <path> <content> <expected exit>
    git rm -q -r --cached . >/dev/null 2>&1 || true
    rm -rf port/* tools/port/*
    printf '%s\n' "$2" > "$1"
    s=0; run || s=$?
    [ "$s" = "$3" ] || { echo "test_pre_commit: $1 ($2): exit $s, expected $3" >&2; exit 1; }
}
check port/a.s '.incbin "SLUS_010.32"' 1
check tools/port/a.s '.incbin "SLUS_010.32"' 1
check port/a.c 'int x;' 0
check tools/port/a.py 'print(1)' 0
echo "test_pre_commit: OK"

#!/bin/sh
# Runs ctest with the given arguments and fails if no test matched or any selected test was
# skipped: class B jobs have the disc, so a skip (exit 77) means the test silently did not run.
log=$(mktemp) || exit 1
trap 'rm -f "$log"' EXIT
status=0
ctest --no-tests=error "$@" >"$log" 2>&1 || status=$?
cat "$log"
if grep -q 'did not run' "$log"; then
	echo "ctest_no_skip: a selected test was skipped" >&2
	exit 1
fi
exit "$status"

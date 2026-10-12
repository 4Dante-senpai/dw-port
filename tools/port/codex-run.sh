#!/usr/bin/env bash
# Runs one Codex task non-interactively for BMAD (implementation handoff, review layers) and prints
# only Codex's final message on stdout. The prompt is read from stdin.
# Usage: tools/port/codex-run.sh <read-only|workspace-write> [timeout_s] < prompt
# Env: CODEX_MODEL (default gpt-6.1-sol), CODEX_EFFORT (default high).
set -euo pipefail

sandbox=$1
limit=${2:-570}
root=$(git -C "$(dirname "$0")" rev-parse --show-toplevel)
out=$(mktemp)
log=$(mktemp)
trap 'rm -f "$out" "$log"' EXIT

if ! timeout "$limit" codex exec -s "$sandbox" --ephemeral -C "$root" \
        -m "${CODEX_MODEL:-gpt-6.1-sol}" -c "model_reasoning_effort=\"${CODEX_EFFORT:-high}\"" \
        -o "$out" - >"$log" 2>&1; then
    echo "codex run failed:" >&2
    tail -20 "$log" >&2
    exit 1
fi

[ -s "$out" ] || { echo "codex run returned empty output" >&2; exit 1; }
cat "$out"

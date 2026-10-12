#!/usr/bin/env bash
# Cross-model review layer for BMAD (bmad-build, bmad-code-review): runs Codex read-only over a
# unified diff file and prints its findings as a Markdown list on stdout.
# Usage: tools/port/codex-review.sh <diff_file>
set -euo pipefail

diff_file=$(realpath "$1")
root=$(git -C "$(dirname "$0")" rev-parse --show-toplevel)
out=$(mktemp)
log=$(mktemp)
trap 'rm -f "$out" "$log"' EXIT

if ! timeout 570 codex exec -s read-only --ephemeral -C "$root" \
        -c model_reasoning_effort='"high"' -o "$out" - >"$log" 2>&1 <<EOF
You are an independent code reviewer for this repository. Project rules are in AGENTS.md; read it first.

Review the unified diff at \`$diff_file\`. Read the surrounding code in the repository as needed to
confirm each finding. Do not modify any file.

Look for:
- Correctness bugs, undefined behavior, integer/sign/overflow issues, and Clang-vs-CodeWarrior differences.
- Behavior that diverges from real PlayStation hardware or the original PsyQ libraries, or that relies on
  an unverified assumption about the game or the hardware without marking it as a hypothesis.
- Changes in src/ not guarded by #ifdef DW1_PORT, or that could alter the matching PS1 build.
- Copyrighted data, disc contents or binaries being committed.
- Missing or weak tests for the changed behavior.

Output a Markdown list of findings only, each one with file:line, the problem, and the concrete bad
outcome. No severity, no praise, no summary. If you find nothing, say "No findings".
EOF
then
    echo "codex review failed:" >&2
    tail -20 "$log" >&2
    exit 1
fi

[ -s "$out" ] || { echo "codex review returned empty output" >&2; exit 1; }
cat "$out"

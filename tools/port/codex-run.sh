#!/usr/bin/env bash
# Runs one Codex task non-interactively for BMAD (implementation handoff, review layers) and prints
# only Codex's final message on stdout. The prompt is read from stdin.
# Usage: tools/port/codex-run.sh <read-only|workspace-write|container> [timeout_s] [model] < prompt
#   container: runs Codex inside the port image ($DW_IMAGE, default dw-port:latest), which is the
#   sandbox: the toolchain is native there and Codex gets no Docker socket.
# model defaults to $CODEX_MODEL, then gpt-6.1-sol. Env: CODEX_EFFORT (default medium).
# Runs are kept in $CODEX_HOME/sessions so their token usage can be audited.
# CODEX_RESUME=<session id> continues that session (e.g. review fixes for the implementer) instead
# of starting a new one, so its context is reused rather than rebuilt.
set -euo pipefail

mode=$1
limit=${2:-570}
model=${3:-${CODEX_MODEL:-gpt-6.1-sol}}
root=$(git -C "$(dirname "$0")" rev-parse --show-toplevel)
tmp=$(mktemp -d)
name=codex-$$
trap 'docker kill "$name" >/dev/null 2>&1 || true; rm -rf "$tmp"' EXIT

if [ "$mode" = container ]; then
    # The repo is mounted at /dw (existing build trees) and at its host path (absolute paths in
    # BMAD prompts). The host's Codex install and credentials are shared with the container.
    export DOCKER_CONTEXT=${DOCKER_CONTEXT:-default}
    codex=(docker run -i --rm --name "$name" --user "$(id -u):$(id -g)" -e HOME=/tmp -e CODEX_HOME=/codex
        -v "$root":/dw -v "$root":"$root" -v dw_venv:/dw/.venv -v "$tmp":"$tmp"
        -v "$(npm root -g)/@openai/codex":/opt/codex:ro -v "${CODEX_HOME:-$HOME/.codex}":/codex
        -w /dw "${DW_IMAGE:-dw-port:latest}" node /opt/codex/bin/codex.js exec)
    sandbox=danger-full-access
else
    codex=(codex exec)
    sandbox=$mode
fi
# exec resume takes no -s/-C: the sandbox goes through config and the session keeps its cwd.
if [ -n "${CODEX_RESUME:-}" ]; then
    codex+=(resume "$CODEX_RESUME" -c "sandbox_mode=\"$sandbox\"")
elif [ "$mode" = container ]; then
    codex+=(-s "$sandbox" -C /dw)
else
    codex+=(-s "$sandbox" -C "$root")
fi

if ! timeout "$limit" "${codex[@]}" -m "$model" \
        -c "model_reasoning_effort=\"${CODEX_EFFORT:-medium}\"" -o "$tmp/out" - >"$tmp/log" 2>&1; then
    echo "codex run failed:" >&2
    tail -20 "$tmp/log" >&2
    exit 1
fi

[ -s "$tmp/out" ] || { echo "codex run returned empty output" >&2; exit 1; }
cat "$tmp/out"

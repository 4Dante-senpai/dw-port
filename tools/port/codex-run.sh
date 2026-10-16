#!/usr/bin/env bash
# Runs one Codex task non-interactively for BMAD (implementation handoff, review layers) and prints
# only Codex's final message on stdout. The prompt is read from stdin.
# Usage: tools/port/codex-run.sh <read-only|workspace-write|container> [timeout_s] [model] < prompt
#   container: runs Codex inside the port image ($DW_IMAGE, default dw-port:latest), which is the
#   sandbox: the toolchain is native there and Codex gets no Docker socket.
# model defaults to $CODEX_MODEL, then gpt-6.1-sol. Env: CODEX_EFFORT (default medium),
# CODEX_COMPACT_LIMIT (default 150000 tokens: at 100000 runs compacted mid-task and re-read
# files; at 250000 every call resent up to 245k cached tokens and a resumed run compacted at once).
# Runs are kept in $CODEX_HOME/sessions so their token usage can be audited.
# CODEX_RESUME=<session id> continues that session (e.g. review fixes for the implementer) instead
# of starting a new one, so its context is reused rather than rebuilt.
# AGENT_LABEL (default <project>/<story>/<AGENT_ROLE or codex>, story taken from a story/<n-m>-…
# branch) and the caller's CLAUDE_CODE_SESSION_ID reach the Codex process, also inside the
# container, so the AI Usage Hub can name the run and link it to the Claude session that launched it.
set -euo pipefail

mode=$1
limit=${2:-570}
model=${3:-${CODEX_MODEL:-gpt-6.1-sol}}
root=$(git -C "$(dirname "$0")" rev-parse --show-toplevel)
branch=$(git -C "$root" rev-parse --abbrev-ref HEAD)
[[ $branch =~ ^story/([0-9]+-[0-9]+) ]] && story=${BASH_REMATCH[1]} || story=${branch//\//-}
project=$(basename "$(dirname "$(git -C "$root" rev-parse --path-format=absolute --git-common-dir)")")
export AGENT_LABEL=${AGENT_LABEL:-$project/$story/${AGENT_ROLE:-codex}}
tmp=$(mktemp -d)
name=codex-$$
trap 'docker kill "$name" >/dev/null 2>&1 || true; rm -rf "$tmp"' EXIT

if [ "$mode" = container ]; then
    # The repo is mounted at /dw (existing build trees) and at its host path (absolute paths in
    # BMAD prompts). The host's Codex install and credentials are shared with the container.
    export DOCKER_CONTEXT=${DOCKER_CONTEXT:-default}
    codex=(docker run -i --rm --name "$name" --user "$(id -u):$(id -g)" -e HOME=/tmp -e CODEX_HOME=/codex
        -e AGENT_LABEL -e CLAUDE_CODE_SESSION_ID
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
        -c "model_reasoning_effort=\"${CODEX_EFFORT:-medium}\"" \
        -c "model_auto_compact_token_limit=${CODEX_COMPACT_LIMIT:-150000}" -o "$tmp/out" - >"$tmp/log" 2>&1; then
    echo "codex run failed:" >&2
    tail -20 "$tmp/log" >&2
    exit 1
fi

[ -s "$tmp/out" ] || { echo "codex run returned empty output" >&2; exit 1; }
cat "$tmp/out"

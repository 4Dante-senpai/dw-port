#!/usr/bin/env bash
# Progress digest of a running tools/port/codex-run.sh task, one block every interval, for the
# Claude session that launched it (run under its Monitor tool so each block reaches the chat).
# Each block: state (activo / SIN ACTIVIDAD N min / TERMINADO), minutes since the session started,
# tool calls, the last assistant commentary and the files changed in the repo. Exits once no
# codex-* container is left. Sessions are the ones codex-run.sh keeps in $CODEX_HOME/sessions.
# Usage: tools/port/codex-watch.sh [interval_s (default 600)] [session.jsonl (default: newest)]
set -uo pipefail

every=${1:-600}
root=$(git -C "$(dirname "$0")" rev-parse --show-toplevel)
f=${2:-$(ls -t "${CODEX_HOME:-$HOME/.codex}"/sessions/*/*/*/rollout-*.jsonl | head -1)}
export DOCKER_CONTEXT=${DOCKER_CONTEXT:-default}

while true; do
    sleep "$every"
    running=$(docker ps --filter name=codex- --format '{{.Names}}' | head -1)
    python3 -I - "$f" "$running" <<'EOF'
import calendar, json, os, sys, time
f, running = sys.argv[1], sys.argv[2]
rows = []
for line in open(f):
    try:
        rows.append(json.loads(line))
    except json.JSONDecodeError:
        continue
calls = sum(1 for r in rows if (r.get("payload") or {}).get("type") in ("custom_tool_call", "function_call"))
msgs = [c["text"] for r in rows if (r.get("payload") or {}).get("type") == "message"
        and r["payload"].get("role") == "assistant"
        for c in r["payload"]["content"] if c.get("type") == "output_text"]
age = int(time.time() - os.path.getmtime(f))
mins = int((time.time() - calendar.timegm(time.strptime(rows[0]["timestamp"][:19], "%Y-%m-%dT%H:%M:%S"))) / 60)
state = "TERMINADO" if not running else (f"SIN ACTIVIDAD {age // 60} min" if age > 600 else "activo")
last = " ".join(msgs[-2:]).replace("\n", " ")[-700:] if msgs else "-"
print(f"[codex {state}] {mins} min, {calls} tool calls, última actividad hace {age}s | último mensaje: {last}")
EOF
    echo "[archivos] $(git -C "$root" status --porcelain | awk '{print $2}' | tr '\n' ' ')"
    [ -n "$running" ] || exit 0
done

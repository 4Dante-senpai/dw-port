#!/usr/bin/env bash
# Progress digest of a background Claude subagent (e.g. the Sonnet implementer of bmad-build), one
# block every interval, for the session that launched it (run under its Monitor tool so each block
# reaches the chat). Each block: state (activo / SIN ACTIVIDAD N min / TERMINADO), minutes since the
# subagent started, tool calls, its last commentary and the files changed in the repo. Watches the
# newest subagent transcript of $CLAUDE_CODE_SESSION_ID (or the one given) and exits once its
# meta.json says finished.
# Usage: tools/port/agent-watch.sh [interval_s (default 600)] [agent-*.jsonl]
set -uo pipefail

every=${1:-600}
root=$(git -C "$(dirname "$0")" rev-parse --show-toplevel)
dir=$HOME/.claude/projects/$(echo "$root" | tr '/.' '--')/${CLAUDE_CODE_SESSION_ID:?}/subagents
f=${2:-$(ls -t "$dir"/agent-*.jsonl 2>/dev/null | head -1)}
[ -n "$f" ] || { echo "agent-watch: no subagent transcript in $dir" >&2; exit 1; }
echo "agent-watch: $f" >&2

while true; do
    sleep "$every"
    python3 -I - "$f" "${f%.jsonl}.meta.json" <<'PY'
import calendar, json, os, sys, time
f, meta = sys.argv[1], sys.argv[2]
rows = []
for line in open(f):
    try:
        rows.append(json.loads(line))
    except json.JSONDecodeError:
        continue
content = [c for r in rows if r.get("type") == "assistant"
           for c in (r.get("message") or {}).get("content") or [] if isinstance(c, dict)]
calls = sum(1 for c in content if c.get("type") == "tool_use")
msgs = [c["text"] for c in content if c.get("type") == "text" and c.get("text", "").strip()]
try:
    done = json.load(open(meta)).get("finished", False)
except (OSError, ValueError):
    done = False
age = int(time.time() - os.path.getmtime(f))
t0 = next(r["timestamp"] for r in rows if r.get("timestamp"))
mins = int((time.time() - calendar.timegm(time.strptime(t0[:19], "%Y-%m-%dT%H:%M:%S"))) / 60)
state = "TERMINADO" if done else (f"SIN ACTIVIDAD {age // 60} min" if age > 600 else "activo")
last = " ".join(msgs[-2:]).replace("\n", " ")[-700:] if msgs else "-"
print(f"[agente {state}] {mins} min, {calls} tool calls, última actividad hace {age}s | último mensaje: {last}")
sys.exit(3 if done else 0)
PY
    rc=$?
    echo "[archivos] $(git -C "$root" status --porcelain | awk '{print $2}' | tr '\n' ' ')"
    [ "$rc" = 3 ] && exit 0
    [ "$rc" = 0 ] || exit 1
done

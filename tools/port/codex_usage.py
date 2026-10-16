#!/usr/bin/env python3
"""Token usage per task of kept Codex sessions: model calls, compactions, input (cached and
uncached), output and peak context.
Usage: tools/port/codex_usage.py ~/.codex/sessions/YYYY/MM/DD/rollout-*.jsonl"""
import json, sys
for path in sys.argv[1:]:
    rows = [json.loads(l) for l in open(path)]
    meta = rows[0]["payload"]
    print(f"\n== {meta.get('id')} cwd={meta.get('cwd')}")
    seg = None; segs = []
    prev_total = None
    for r in rows:
        t, p = r.get("type"), r.get("payload", {}) or {}
        if t == "event_msg" and p.get("type") == "task_started":
            seg = dict(start=r["timestamp"], calls=0, inp=0, cached=0, out=0, reas=0, maxctx=0, compact=0, tools=0); segs.append(seg)
        elif seg is None:
            continue
        elif t == "event_msg" and p.get("type") == "token_count" and p.get("info"):
            tot = p["info"]["total_token_usage"]; last = p["info"]["last_token_usage"]
            if tot == prev_total: continue
            prev_total = tot
            seg["calls"] += 1; seg["inp"] += last["input_tokens"]; seg["cached"] += last["cached_input_tokens"]
            seg["out"] += last["output_tokens"]; seg["reas"] += last.get("reasoning_output_tokens", 0)
            seg["maxctx"] = max(seg["maxctx"], last["input_tokens"])
        elif t == "compacted":
            seg["compact"] += 1
        elif t == "response_item" and p.get("type") in ("custom_tool_call", "function_call"):
            seg["tools"] += 1
        elif t == "event_msg" and p.get("type") == "task_complete":
            seg["end"] = r["timestamp"]
        elif t == "turn_context" and "model" not in seg:
            seg["model"] = p.get("model"); seg["effort"] = p.get("effort") or p.get("reasoning_effort")
    for i, s in enumerate(segs, 1):
        print(f" task{i} {s['start'][11:19]}-{s.get('end','?')[11:19]} model={s.get('model')} effort={s.get('effort')} "
              f"calls={s['calls']} tools={s['tools']} compactions={s['compact']} input={s['inp']:,} cached={s['cached']:,} "
              f"uncached={s['inp']-s['cached']:,} output={s['out']:,} (reasoning {s['reas']:,}) max_ctx={s['maxctx']:,}")

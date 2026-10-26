#!/usr/bin/env python3
"""Summarize hardware breakpoint observations without summing overlapping spans."""
import argparse
from collections import Counter, defaultdict
import hashlib
import json
from pathlib import Path


def summarize(path):
    previous = -1
    origin = None
    counts = Counter()
    commands = []
    spans = defaultdict(list)
    pending = {}
    dma = defaultdict(list)
    points = []
    segments = []
    segment_start = None
    operations = defaultdict(Counter)
    api = defaultdict(lambda: dict(count=0, inclusive_cycles=0, own_cycles=0, crosses_start=0))

    def finish_segment(label, cycle):
        nonlocal segment_start
        segments.append(dict(start_cycle=segment_start-origin, end_cycle=cycle-origin,
            end_checkpoint=label, operations={key: dict(value) for key, value in sorted(operations.items())},
            api={key: dict(value) for key, value in sorted(api.items())}))
        segment_start = cycle
        operations.clear()
        api.clear()
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for line in stream:
            digest.update(line)
            row = json.loads(line)
            cycle = row.get("cycle")
            if type(cycle) is not int or cycle < previous:
                raise ValueError(f"{path}: backwards or invalid hardware cycle")
            previous = cycle
            device, event = row.get("device"), row.get("event")
            if device not in ("cd", "gpu", "mdec", "card", "cpu", "checkpoint") or not isinstance(event, str):
                raise ValueError(f"{path}: invalid hardware event")
            if event == "frame0":
                if origin is not None:
                    raise ValueError(f"{path}: duplicate hardware frame 0")
                origin = cycle
                segment_start = cycle
                pending.clear()
            if origin is None:
                continue
            counts[device + ":" + event] += 1
            if device == "checkpoint" and event in ("frame0", "mark", "checkpoint"):
                points.append(dict(label=row["label"], event=event, cycle=cycle-origin))
                if event == "checkpoint":
                    finish_segment(row["label"], cycle)
            elif event == "function_span":
                start, elapsed = row.get("start"), row.get("elapsed")
                if type(start) is not int or type(elapsed) is not int or elapsed < 0 or start+elapsed != cycle:
                    raise ValueError(f"{path}: invalid function span")
                spans[row["label"]].append(elapsed)
                entry = api[row["label"]]
                entry["count"] += 1
                entry["inclusive_cycles"] += elapsed
                entry["own_cycles"] += row.get("own", elapsed)
                entry["crosses_start"] += start < segment_start
            elif device == "cd" and event == "command":
                if type(row.get("command")) is not int or not 0 <= row["command"] <= 255:
                    raise ValueError(f"{path}: invalid CD command")
                commands.append(dict(cycle=cycle-origin, command=row["command"], parameters=row["parameters"]))
                operations["cd_commands"][f'{row["command"]:02x}'] += 1
            elif event == "dma":
                channel = row["channel"]
                if channel not in range(4) or type(row["words"]) is not int or row["words"] < 0:
                    raise ValueError(f"{path}: invalid DMA request")
                pending[channel] = row
                operations[f"dma{channel}"]["requests"] += 1
                operations[f"dma{channel}"]["words"] += row["words"]
            elif event == "dma_done":
                channel = row["channel"]
                request = pending.pop(channel, None)
                lower, upper = row["previous_busy"], row["elapsed"]
                if request is None or not 0 <= lower <= upper or request["cycle"]+upper != cycle:
                    raise ValueError(f"{path}: invalid DMA completion bounds")
                dma[str(channel)].append(dict(cycle=request["cycle"]-origin, words=request["words"],
                    bcr=request["bcr"], control=request["control"], command=request.get("command"),
                    previous_busy=lower, observed_done=upper))
            elif event in ("loadMap_enter", "loadMap_return"):
                points.append(dict(label=event, event=event, cycle=cycle-origin))
            elif device == "gpu" and event in ("gp0", "gp1"):
                operations[event][f'{row["command"]:02x}'] += 1
            elif device == "mdec" and event == "command":
                operations["mdec_commands"][f'{row["value"] >> 29:x}'] += 1
            elif device == "cpu" and event == "irq_span":
                operations["irq"]["calls"] += 1
                operations["irq"]["inclusive_cycles"] += row["elapsed"]
            elif device == "cpu" and event == "pad_poll":
                operations["pad_poll"]["calls"] += 1
                operations["pad_poll"]["serial_cycles"] += row["elapsed"]
    if origin is None:
        raise ValueError(f"{path}: missing hardware frame 0")
    return dict(sha256=digest.hexdigest(), origin_cycle=origin, counts=dict(sorted(counts.items())),
        points=points, cd_commands=commands, dma=dict(dma), segments=segments,
        functions={name: dict(count=len(values), minimum=min(values), maximum=max(values),
                             inclusive_total=sum(values)) for name, values in sorted(spans.items())})


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", type=Path)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    args.out.write_text(json.dumps(summarize(args.trace), indent=2) + "\n")

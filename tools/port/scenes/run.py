#!/usr/bin/env python3
"""Run a native scene twice and verify display and virtual-clock determinism."""
import argparse
import hashlib
import os
from pathlib import Path
import re
import subprocess
import sys
import tomllib

ROOT = Path(__file__).resolve().parents[3]


def ppm_colors(path):
    data = path.read_bytes()
    header = re.match(rb"P6\s+(\d+)\s+(\d+)\s+255\n", data)
    if not header:
        raise ValueError(f"invalid P6 capture: {path}")
    pixels = data[header.end():]
    if len(pixels) != int(header[1]) * int(header[2]) * 3:
        raise ValueError(f"truncated P6 capture: {path}")
    return len({pixels[i:i + 3] for i in range(0, len(pixels), 3)})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("id", choices=["title"])
    parser.add_argument("--build", required=True, choices=["native-headless", "native-asan"])
    parser.add_argument("--pack", type=Path)
    args = parser.parse_args()
    scene = tomllib.loads((ROOT / "tools/port/scenes" / f"{args.id}.toml").read_text())
    build = ROOT / "build/port" / args.build
    pack = args.pack.resolve() if args.pack else build / "us.pack"
    if not pack.is_file():
        if args.pack is not None:
            raise ValueError(f"Pack file does not exist: {pack}")
        disc = os.environ.get("DW1_DISC_DIR")
        if not disc:
            print("scene_title: no Pack or DW1_DISC_DIR; skipped")
            return 77
        cues = sorted(Path(disc).glob("*.cue"))
        if not cues:
            raise ValueError(f"no .cue in {disc}")
        subprocess.run([str(build / "dw1-pack"), "build", str(cues[0]),
                        str(ROOT / "tools/port/manifest/us.tsv"), str(pack)], check=True)
    output = build / "scenes" / args.id
    output.mkdir(parents=True, exist_ok=True)
    results = []
    for index in (1, 2):
        capture = output / f"run-{index}.ppm"
        trace = output / f"run-{index}.trace"
        clock = output / f"run-{index}.clock"
        for path in (capture, trace, clock):
            path.unlink(missing_ok=True)
        env = dict(os.environ, DW1_TRACE=str(trace))
        if args.build == "native-asan":
            # Same as the native-asan test preset: the fake stack would move game-stack
            # primitives out of the OT window (AD-5).
            env["ASAN_OPTIONS"] = env.get("ASAN_OPTIONS", "") + ":detect_stack_use_after_return=0"
        command = [str(build / "dw1"), "--headless", "--pack", str(pack),
                   "--frames", "10000", "--capture-mark", scene["mark"],
                   "--capture-vblank", str(scene["capture_vblank"]),
                   "--capture", str(capture), "--clock-trace", str(clock)]
        run = subprocess.run(command, cwd=build, env=env, capture_output=True, timeout=600)
        log = run.stdout + run.stderr
        (output / f"run-{index}.log").write_bytes(log)
        if run.returncode or re.search(rb"AddressSanitizer|runtime error", log):
            raise ValueError(f"run {index}: exit {run.returncode}\n{log.decode(errors='replace')}")
        if not re.search(rf"^\d+ mark {re.escape(scene['mark'])}$", trace.read_text(), re.M):
            raise ValueError(f"run {index}: missing scene mark")
        colors = ppm_colors(capture)
        if colors <= 16:
            raise ValueError(f"run {index}: only {colors} colors")
        hashes = tuple(hashlib.sha256(path.read_bytes()).hexdigest() for path in (capture, clock))
        if not clock.stat().st_size:
            raise ValueError("empty clock trace")
        results.append(hashes)
        print(f"{capture}: sha256={hashes[0]} colors={colors}")
        print(f"{clock}: sha256={hashes[1]}")
    if results[0] != results[1]:
        raise ValueError("capture or clock trace differs between runs")
    print(f"scene_title: {args.build}: OK (two identical captures and clock traces)")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print(f"scene_title: {error}", file=sys.stderr)
        sys.exit(1)

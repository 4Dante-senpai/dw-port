#!/usr/bin/env python3
"""Run a native scene twice; optionally compare RGB555 against its pinned reference."""
import argparse
import hashlib
import os
from pathlib import Path
import re
import subprocess
import sys
import tomllib

ROOT = Path(__file__).resolve().parents[3]


def read_ppm(path):
    data = path.read_bytes()
    header = re.match(rb"P6\s+(\d+)\s+(\d+)\s+255\n", data)
    if not header:
        raise ValueError(f"invalid P6 capture: {path}")
    pixels = data[header.end():]
    if len(pixels) != int(header[1]) * int(header[2]) * 3:
        raise ValueError(f"truncated P6 capture: {path}")
    return int(header[1]), int(header[2]), pixels


def ppm_colors(path):
    pixels = read_ppm(path)[2]
    return len({pixels[i:i + 3] for i in range(0, len(pixels), 3)})


def reference_path(scene_id):
    return Path(os.environ.get("DW1_REFERENCE_DIR", str(ROOT / "build/reference"))) / scene_id / "run-1.ppm"


def validate_comparison(scene, scene_id):
    expected = scene.get("reference_sha256")
    if not isinstance(expected, str) or not re.fullmatch(r"[0-9a-f]{64}", expected):
        raise ValueError(f"scene {scene_id}: missing or invalid reference_sha256")
    reference = reference_path(scene_id)
    frame = read_ppm(reference) if reference.is_file() else None
    if frame and hashlib.sha256(reference.read_bytes()).hexdigest() != expected:
        raise ValueError(f"scene {scene_id}: local reference does not match {expected}; run capture.py")
    exceptions = scene.get("exceptions", [])
    if not isinstance(exceptions, list):
        raise ValueError("exceptions must be an array of tables")
    for entry in exceptions:
        region = entry.get("region") if isinstance(entry, dict) else None
        reason = entry.get("reason") if isinstance(entry, dict) else None
        if (not isinstance(region, list) or len(region) != 4
                or any(type(v) is not int for v in region)
                or not isinstance(reason, str) or not reason.strip()):
            raise ValueError("exception requires region = [x0, y0, x1, y1] and a nonempty reason")
        if frame is None:
            raise ValueError("cannot validate exception bounds without reference; run capture.py")
        x0, y0, x1, y1 = region
        if not (0 <= x0 <= x1 < frame[0] and 0 <= y0 <= y1 < frame[1]):
            raise ValueError(f"exception region outside frame: {region}")
    return frame


def compare_capture(capture, scene, scene_id, frame):
    actual_hash = hashlib.sha256(capture.read_bytes()).hexdigest()
    expected = scene["reference_sha256"]
    if frame is None:
        if actual_hash == expected:
            print(f"scene {scene_id}: reference match {expected}")
            return 0
        raise ValueError(f"scene {scene_id}: port sha256={actual_hash}, expected={expected}; "
                         "local reference PPM missing; run tools/port/reference/capture.py")
    width, height, pixels = read_ppm(capture)
    if (width, height) != frame[:2]:
        raise ValueError(f"scene {scene_id}: dimensions {width}x{height} != {frame[0]}x{frame[1]}")
    exceptions = scene.get("exceptions", [])
    counts = [0] * len(exceptions)
    outside = []
    diff = bytearray(len(pixels))
    for index in range(width * height):
        offset = index * 3
        if all(pixels[offset + c] >> 3 == frame[2][offset + c] >> 3 for c in range(3)):
            continue
        x, y = index % width, index // width
        diff[offset:offset + 3] = bytes((255, 0, 255))
        covered = False
        for k, entry in enumerate(exceptions):
            x0, y0, x1, y1 = entry["region"]
            if x0 <= x <= x1 and y0 <= y <= y1:
                counts[k] += 1
                covered = True
                break  # Overlapping regions count a pixel once, in declaration order.
        if not covered:
            outside.append((x, y))
    for entry, count in zip(exceptions, counts):
        print(f"exception {entry['region']}: {entry['reason']} ({count} px)")
    diff_path = capture.with_name("diff.ppm")
    if outside or any(counts):
        diff_path.write_bytes(f"P6\n{width} {height}\n255\n".encode() + diff)
    if outside:
        xs, ys = zip(*outside)
        print(f"{len(outside)} pixels differ, box {min(xs)}..{max(xs)} {min(ys)}..{max(ys)}; {diff_path}")
        return 1
    print(f"scene {scene_id}: RGB555 reference match ({sum(counts)} excepted pixels)")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("id", choices=["title"])
    parser.add_argument("--build", required=True, choices=["native-headless", "native-asan"])
    parser.add_argument("--pack", type=Path)
    parser.add_argument("--compare", action="store_true")
    args = parser.parse_args()
    build = ROOT / "build/port" / args.build
    output = build / "scenes" / args.id
    if args.compare:
        (output / "diff.ppm").unlink(missing_ok=True)
    scene = tomllib.loads((ROOT / "tools/port/scenes" / f"{args.id}.toml").read_text())
    frame = validate_comparison(scene, args.id) if args.compare else None
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
    if args.compare and compare_capture(output / "run-1.ppm", scene, args.id, frame):
        return 1
    print(f"scene_title: {args.build}: OK (two identical captures and clock traces)")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print(f"scene_title: {error}", file=sys.stderr)
        sys.exit(1)

#!/usr/bin/env python3
"""Capture complete native title_audio PCM and absolute inputs without Redux or cards."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess
import tomllib

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location("scene_run", Path(__file__).with_name("run.py"))
scene_run = importlib.util.module_from_spec(spec)
spec.loader.exec_module(scene_run)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pack", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=Path(os.environ.get("DW1_PCM_DIR", "/tmp/dw1-web-title-audio")))
    args = parser.parse_args()
    if not os.environ.get("DW1_DISC_DIR"):
        print("native_title_audio: DW1_DISC_DIR not set; skipped")
        return 77
    output = args.output.resolve()
    if output.is_relative_to(ROOT) or any(c.isspace() for c in str(output)):
        raise ValueError("PCM fixture must be outside repository and contain no whitespace")
    output.mkdir(parents=True, exist_ok=True)
    (output / "fixture.json").unlink(missing_ok=True)
    scene = tomllib.loads((ROOT / "tools/port/scenes/title_audio.toml").read_text())
    scene_run.validate_scene(scene)
    scene_run.audio.validate_segments(scene)
    binary = ROOT / "build/port/native-headless/dw1"
    checkpoints = scene_run.checkpoint_symbols(scene, ROOT / "config/us/symbols.txt", binary)
    inputs, cp_file, segments = scene_run.write_audio_files(scene, checkpoints, output, 1)
    command = [str(binary), "--headless", "--pack", str(args.pack.resolve()), "--frames", "40000",
               "--capture-mark", scene["mark"], "--inputs-from", "mark", "--inputs", str(inputs),
               "--checkpoints", str(cp_file), "--audio-segments", str(segments),
               "--audio-pcm", str(output / "native.pcm"), "--resolved-inputs", str(output / "replay.txt")]
    result = subprocess.run(command, cwd=binary.parent, capture_output=True, timeout=1800)
    log = result.stdout + result.stderr
    (output / "native.log").write_bytes(log)
    if result.returncode or b"runtime error" in log or b"AddressSanitizer" in log:
        raise ValueError(f"native PCM failed ({result.returncode}); see {output}/native.log")
    final = re.search(rb"final absolute VBlank (\d+)", log)
    if not final: raise ValueError("native did not report final absolute VBlank")
    pcm = (output / "native.pcm").read_bytes()
    if not pcm or len(pcm) % 4 or not any(pcm): raise ValueError("invalid or silent native PCM")
    record = {"final_vblank": int(final[1]), "frames": len(pcm) // 4,
              "sha256": hashlib.sha256(pcm).hexdigest(),
              "inputs_sha256": hashlib.sha256((output / "replay.txt").read_bytes()).hexdigest()}
    (output / "fixture.json").write_text(json.dumps(record, indent=2) + "\n")
    print(json.dumps(record))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

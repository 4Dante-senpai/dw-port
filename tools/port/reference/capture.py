#!/usr/bin/env python3
"""Capture a scene twice with the pinned PCSX-Redux reference emulator."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import signal
import struct
import subprocess
import sys
import tomllib

ROOT = Path(__file__).resolve().parents[3]
OPENBIOS = Path("/usr/local/share/pcsx-redux/openbios.bin")
_spec = importlib.util.spec_from_file_location("scene_runner", ROOT / "tools/port/scenes/run.py")
_runner = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_runner)
ppm_colors = _runner.ppm_colors


def resolve_symbols(path, mark):
    symbols = {name: int(address, 16) for name, address in re.findall(
        r"^([\w.$]+)\s*=\s*(0x[0-9a-fA-F]+);", path.read_text(), re.M)}
    for name in ("VSync", mark):
        if name not in symbols:
            raise ValueError(f"missing symbol: {name}")
    return dict(vsync=symbols["VSync"], mark=symbols[mark])


def to_ppm(raw, width, height, bpp):
    if not (1 <= width <= 640 and 1 <= height <= 480) or bpp not in (16, 24):
        raise ValueError(f"invalid display: {width}x{height} {bpp} bpp")
    if len(raw) != width * height * (bpp // 8):
        raise ValueError("invalid raw display length")
    if bpp == 24:
        rgb = raw
    else:
        rgb = bytearray()
        for (pixel,) in struct.iter_unpack("<H", raw):
            for shift in (0, 5, 10):
                channel = (pixel >> shift) & 31
                rgb.append((channel << 3) | (channel >> 2))
    return f"P6\n{width} {height}\n255\n".encode() + rgb


def lua_literal(value):
    # JSON string escapes are compatible here except non-ASCII: preserve UTF-8.
    if isinstance(value, dict):
        return "{" + ",".join("[" + lua_literal(k) + "]=" + lua_literal(v)
                              for k, v in value.items()) + "}"
    if isinstance(value, list):
        return "{" + ",".join(map(lua_literal, value)) + "}"
    return json.dumps(value, ensure_ascii=False)


def safe_output(path):
    path = path.resolve()
    build = (ROOT / "build").resolve()
    external = os.environ.get("DW1_REFERENCE_DIR")
    if path.is_relative_to(build):
        return path
    if external and path.is_relative_to(Path(external).resolve()) and not path.is_relative_to(ROOT):
        return path
    raise ValueError("output must be in build/ or external DW1_REFERENCE_DIR")


def validate_scene(scene):
    if type(scene.get("capture_vblank")) is not int or scene["capture_vblank"] < 0:
        raise ValueError("capture_vblank must be a nonnegative integer")
    previous = -1
    for event in scene.get("inputs", []):
        if (set(event) != {"vblank", "buttons"} or type(event["vblank"]) is not int
                or event["vblank"] <= previous or not isinstance(event["buttons"], list)
                or any(type(b) is not int or not 0 <= b <= 15 for b in event["buttons"])):
            raise ValueError("inputs require increasing vblank and buttons (PS1 bit indices 0..15)")
        previous = event["vblank"]


def run_once(emulator, cue, bios, scene, symbols, output, index, timeout, max_vblanks):
    work = output / f"work-{index}"
    if work.exists():
        shutil.rmtree(work)
    work.mkdir()
    # Portable configuration prevents host settings, cards or input from leaking in.
    (work / "pcsx.json").write_text(json.dumps({"SPU": {
        "Backend": "dummy", "UseNullSync": True, "Mute": True, "Speed": 0}}))
    config = dict(symbols, capture_vblank=scene["capture_vblank"],
                  inputs=scene.get("inputs", []), max_vblanks=max_vblanks,
                  raw=str(work / "display.raw"), meta=str(work / "display.json"),
                  progress=str(work / "progress.txt"))
    script = work / "capture.lua"
    script.write_text("DW1_CAPTURE = " + lua_literal(config) + "\n" +
                      (ROOT / "tools/port/reference/capture.lua").read_text())
    command = ["xvfb-run", "-a", emulator, "-no-ui", "-portable", str(work),
               "-interpreter", "-softgpu", "-debugger", "-no-fastboot",
               "-2mb", "-1mbvram", "-no-trace", "-no-pcdrv", "-noupdate",
               "-no-webserver", "-no-gdb", "-iso", str(cue), "-bios", str(bios),
               "-memcard1", str(work / "card1.mcd"), "-memcard2", str(work / "card2.mcd"),
               "-dofile", str(script), "-run"]
    log = output / f"run-{index}.log"
    with log.open("wb") as stream:
        process = subprocess.Popen(command, cwd=work, stdout=stream, stderr=subprocess.STDOUT,
                                   start_new_session=True)
        try:
            code = process.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGKILL)
            process.wait()
            code = "wall timeout"
    progress = (work / "progress.txt").read_text().strip() if (work / "progress.txt").exists() else "before frame 0"
    if code != 0 or not (work / "display.json").exists():
        raise ValueError(f"run {index}: {code}; last point: {progress}; log: {log}")
    meta = json.loads((work / "display.json").read_text())
    capture = output / f"run-{index}.ppm"
    capture.write_bytes(to_ppm((work / "display.raw").read_bytes(), **meta))
    colors = ppm_colors(capture)
    if colors <= 16:
        raise ValueError(f"run {index}: only {colors} colors; log: {log}")
    digest = hashlib.sha256(capture.read_bytes()).hexdigest()
    print(f"{capture}: {meta['width']}x{meta['height']} colors={colors} sha256={digest}")
    return digest


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("id", choices=["title"])
    parser.add_argument("--update", action="store_true")
    parser.add_argument("--bios", type=Path)
    parser.add_argument("--out", type=Path)
    parser.add_argument("--timeout", type=float, default=600)
    parser.add_argument("--max-vblanks", type=int, default=10000)
    args = parser.parse_args(argv)
    if args.bios and args.update:
        raise ValueError("--bios cannot be combined with --update")
    if args.timeout <= 0 or args.max_vblanks <= 0:
        raise ValueError("timeouts must be positive")
    bios = args.bios.resolve() if args.bios else OPENBIOS
    if args.bios and (not bios.is_file() or bios.is_relative_to(ROOT)):
        raise ValueError("--bios must be an existing file outside the repository")
    disc = os.environ.get("DW1_DISC_DIR")
    cues = sorted(Path(disc).glob("*.cue")) if disc else []
    if not cues:
        print("reference_title: no disc (.cue) in DW1_DISC_DIR; skipped")
        return 77
    emulator = shutil.which("pcsx-redux")
    if not emulator or not shutil.which("xvfb-run"):
        print("reference_title: pcsx-redux or xvfb-run absent from PATH; skipped")
        return 77
    if not bios.is_file():
        raise ValueError(f"OpenBIOS missing: {bios}")
    scene_path = ROOT / "tools/port/scenes" / f"{args.id}.toml"
    scene = tomllib.loads(scene_path.read_text())
    validate_scene(scene)
    symbols = resolve_symbols(ROOT / "config/us/symbols.txt", scene["mark"])
    output = safe_output(args.out or Path(os.environ.get(
        "DW1_REFERENCE_DIR", str(ROOT / "build/reference"))) / args.id)
    output.mkdir(parents=True, exist_ok=True)
    # Remove stale captures before either run (failed invocations cannot reuse evidence).
    for index in (1, 2):
        (output / f"run-{index}.ppm").unlink(missing_ok=True)
    hashes = [run_once(emulator, cues[0].resolve(), bios, scene, symbols, output,
                       index, args.timeout, args.max_vblanks) for index in (1, 2)]
    if hashes[0] != hashes[1]:
        raise ValueError(f"non-deterministic captures: {hashes[0]} != {hashes[1]}")
    expected = scene.get("reference_sha256")
    if args.update:
        source = scene_path.read_text()
        line = f'reference_sha256 = "{hashes[0]}"'
        source = re.sub(r'^reference_sha256\s*=.*$', line, source, flags=re.M) if expected else source.rstrip() + "\n" + line + "\n"
        scene_path.write_text(source)
    elif expected and not args.bios and hashes[0] != expected:
        raise ValueError(f"reference hash mismatch: actual={hashes[0]} expected={expected}")
    if args.bios:
        print(f"owner BIOS: OpenBIOS hash={expected or 'unset'}; captured={hashes[0]}")
    print(f"reference_{args.id}: OK sha256={hashes[0]}" + (" (unpinned)" if not expected and not args.update else ""))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print(f"reference_title: {error}", file=sys.stderr)
        sys.exit(1)

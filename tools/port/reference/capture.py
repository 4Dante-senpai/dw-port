#!/usr/bin/env python3
"""Capture a scene twice with the pinned PCSX-Redux reference emulator."""
import argparse
from concurrent.futures import ThreadPoolExecutor
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
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[3]
_spec = importlib.util.spec_from_file_location("scene_runner", ROOT / "tools/port/scenes/run.py")
_runner = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_runner)
OPENBIOS = _runner.OPENBIOS
ppm_colors = _runner.ppm_colors


def resolve_symbols(path, mark, overlay=None):
    source=path.read_text()
    extra=path.with_name(f"symbols_{overlay}.txt")
    if overlay and extra.is_file(): source += "\n" + extra.read_text()
    symbols = {name: int(address, 16) for name, address in re.findall(
        r"^([\w.$]+)\s*=\s*(0x[0-9a-fA-F]+);", source, re.M)}
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


validate_scene = _runner.validate_scene


def pointer_symbols(source):
    entries = []
    for name, address, comment in re.findall(
            r"^([\w.$]+)\s*=\s*(0x[0-9a-fA-F]+);([^\n]*)", source, re.M):
        size = re.search(r"\bsize:(0x[0-9a-fA-F]+)", comment)
        entries.append((name, int(address, 16), int(size[1], 16) if size else None,
                        "type:func" in comment))
    entries.sort(key=lambda entry: entry[1])
    result = []
    for i, (name, address, size, function) in enumerate(entries):
        if 0x80100000 <= address < 0x80200000 and not function:
            next_address = next((entry[1] for entry in entries[i + 1:] if entry[1] > address), address + 1)
            result.append(dict(name=name, address=address, size=size or next_address - address))
    return result


def run_once(emulator, cue, bios, scene, symbols, output, index, timeout, max_vblanks):
    work = output / f"work-{index}"
    if work.exists():
        shutil.rmtree(work)
    work.mkdir()
    # Portable configuration prevents host settings, cards or input from leaking in.
    audio = scene.get("audio")
    settings = dict(Backend="dummy", UseNullSync=True, Mute=False, Speed=1,
                    Interp=2, KeyOnDelay=6) if audio else dict(
                        Backend="dummy", UseNullSync=True, Mute=True, Speed=0)
    (work / "pcsx.json").write_text(json.dumps({"SPU": settings}))
    config = dict(symbols, capture_vblank=scene["capture_vblank"],
                  inputs=scene.get("inputs", []), max_vblanks=max_vblanks,
                  raw=str(work / "display.raw"), meta=str(work / "display.json"),
                  progress=str(work / "progress.txt"), inputs_from=scene.get("inputs_from", "frame0"))
    config["checkpoints"] = scene.get("resolved_checkpoints", [])
    if audio:
        config["audio_drain_vblanks"] = 4
        config["audio"] = [dict(segment, stop_addresses=_runner.checkpoint_symbols(
                                dict(checkpoints=segment.get("stop_on", [])), ROOT / "config/us/symbols.txt"), trace=str(output / f"run-{index}-{i}.spu"),
                                meta=str(output / f"run-{index}-{i}.json"))
                           for i, segment in enumerate(audio, 1)]
    if scene.get("movie"):
        directory = output / f"movie-{index}"
        if directory.exists(): shutil.rmtree(directory)
        directory.mkdir()
        config["movie"] = str(directory)
        config["movie_display"] = _runner.checkpoint_symbols(
            dict(checkpoints=["PutDispEnv"]), ROOT / "config/us/symbols.txt")[0]["address"]
    if scene.get("_state_vars"):
        config.update(state_vars=scene["_state_vars"], state_symbols=scene["state_symbols"],
                      state_trace=str(output / f"run-{index}.state"))
    script = work / "capture.lua"
    script.write_text("DW1_CAPTURE = " + lua_literal(config) + "\n" +
                      (ROOT / "tools/port/reference/capture.lua").read_text())
    cards = _runner.prepare_cards(scene, "reference", index, scene.get("_card_build", "native-headless"))
    command = ["xvfb-run", "-a", "-n", str(100 * index + 99), emulator, "-no-ui", "-portable", str(work),
               "-interpreter", "-softgpu", "-debugger", "-no-fastboot",
               "-2mb", "-1mbvram", "-no-trace", "-no-pcdrv", "-noupdate",
               "-no-webserver", "-no-gdb", "-iso", str(cue), "-bios", str(bios),
               "-memcard1", str(cards[0]), "-memcard2", str(cards[1]),
               "-dofile", str(script), "-run"]
    log = output / f"run-{index}.log"
    with log.open("wb") as stream:
        environment = dict(os.environ)
        if audio:
            environment["DW1_REDUX_PCM_TAP"] = str(output / f"run-{index}.pcm")
            if scene.get("xa"):
                environment["DW1_REDUX_XA_TAP"] = str(output / f"run-{index}.xa")
        process = subprocess.Popen(command, cwd=work, stdout=stream, stderr=subprocess.STDOUT,
                                   start_new_session=True, env=environment)
        try:
            code = process.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGKILL)
            process.wait()
            code = "wall timeout"
    progress = (work / "progress.txt").read_text().strip() if (work / "progress.txt").exists() else "before frame 0"
    if code != 0 or (not audio and not (work / "display.json").exists()):
        raise ValueError(f"run {index}: {code}; last point: {progress}; log: {log}")
    if "card1" in scene:
        # The emulator flushes both cards when PCSX.quit exits; export only after wait().
        digest = hashlib.sha256(cards[0].read_bytes()).hexdigest()
        (output / f"run-{index}.card1.sha256").write_text(digest + "\n")
    if scene.get("movie"):
        for raw in directory.glob("frame-*.raw"):
            meta = json.loads(raw.with_suffix(".json").read_text())
            raw.with_suffix(".ppm").write_bytes(to_ppm(raw.read_bytes(), **meta))
            raw.unlink(); raw.with_suffix(".json").unlink()
    if audio:
        return progress
    meta = json.loads((work / "display.json").read_text())
    capture = output / f"run-{index}.ppm"
    capture.write_bytes(to_ppm((work / "display.raw").read_bytes(), **meta))
    colors = ppm_colors(capture)
    if colors <= 16 and not scene.get("_state_vars"):
        raise ValueError(f"run {index}: only {colors} colors; log: {log}")
    digest = hashlib.sha256(capture.read_bytes()).hexdigest()
    print(f"{capture}: {meta['width']}x{meta['height']} colors={colors} sha256={digest}")
    return digest


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("id", choices=sorted(p.stem for p in (ROOT / "tools/port/scenes").glob("*.toml")))
    parser.add_argument("--update", action="store_true")
    parser.add_argument("--state", action="store_true")
    parser.add_argument("--card1", type=Path, help="existing raw input card outside the repository")
    parser.add_argument("--card-build", default="native-headless", choices=["native-headless", "native-asan"])
    parser.add_argument("--audio", action="store_true")
    parser.add_argument("--reuse", action="store_true", help="reuse a valid deterministic state/audio capture")
    parser.add_argument("--bios", type=Path)
    parser.add_argument("--out", type=Path)
    parser.add_argument("--timeout", type=float, default=600)
    parser.add_argument("--max-vblanks", type=int, default=10000)
    args = parser.parse_args(argv)
    if args.id in ("movie0", "movie0_skip"): args.audio = True
    if args.reuse and (args.update or not (args.state or args.audio)):
        raise ValueError("--reuse requires --state or --audio and cannot be combined with --update")
    if args.audio and (args.state or args.update):
        raise ValueError("--audio cannot be combined with --state or --update")
    if args.bios and args.update:
        raise ValueError("--bios cannot be combined with --update")
    if args.timeout <= 0 or args.max_vblanks <= 0:
        raise ValueError("timeouts must be positive")
    bios = args.bios.resolve() if args.bios else OPENBIOS
    if args.bios and (not bios.is_file() or bios.is_relative_to(ROOT)):
        raise ValueError("--bios must be an existing file outside the repository")
    scene_path = ROOT / "tools/port/scenes" / f"{args.id}.toml"
    scene = tomllib.loads(scene_path.read_text())
    validate_scene(scene)
    if scene.get("audio") and not args.audio:
        raise ValueError("audio scenes require --audio")
    if args.audio and not scene.get("audio"):
        raise ValueError("--audio requires an audio scene")
    scene["_card_build"] = args.card_build
    if args.card1:
        scene["_card1_input"] = str(args.card1)
        scene.setdefault("card1", "new")
    scene["resolved_checkpoints"] = _runner.checkpoint_symbols(scene, ROOT / "config/us/symbols.txt")
    symbols = resolve_symbols(ROOT / "config/us/symbols.txt", scene["mark"], scene.get("overlay"))
    if args.state:
        scene["_state_vars"] = _runner.state_variables(ROOT / "tools/port/scenes" / scene.get("state_vars", "state_vars.txt"), ROOT / "config/us/symbols.txt")
        source = (ROOT / "config/us/symbols.txt").read_text()
        scene["state_symbols"] = pointer_symbols(source)
        rand = re.search(r"^rand\s*=\s*(0x[0-9a-fA-F]+)", source, re.M)
        if not rand:
            raise ValueError("missing reference symbol: rand")
        symbols["rand"] = int(rand[1], 16)
    disc = os.environ.get("DW1_DISC_DIR")
    cues = sorted(Path(disc).glob("*.cue")) if disc else []
    if not cues:
        print(f"reference {args.id}: no disc (.cue) in DW1_DISC_DIR; skipped")
        return 77
    emulator = shutil.which("pcsx-redux")
    if not emulator or not shutil.which("xvfb-run"):
        print(f"reference {args.id}: pcsx-redux or xvfb-run absent from PATH; skipped")
        return 77
    if not bios.is_file():
        raise ValueError(f"OpenBIOS missing: {bios}")
    if args.audio:
        spec = importlib.util.spec_from_file_location("reference_audio", Path(__file__).with_name("audio.py"))
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        return module.capture_audio(args, scene, symbols, emulator, cues[0].resolve(), bios,
                                    SimpleNamespace(ROOT=ROOT, run_once=run_once))
    output = safe_output(args.out or Path(os.environ.get(
        "DW1_REFERENCE_DIR", str(ROOT / "build/reference"))) / args.id)
    output.mkdir(parents=True, exist_ok=True)
    identity = _runner.bios_fingerprint(bios)
    fingerprint = _runner.state_fingerprint(scene, identity) if args.state else None
    stamp = output / "state.sha256"
    expected = scene.get("reference_sha256")
    if (args.reuse and stamp.is_file() and stamp.read_bytes().strip() == fingerprint.encode()
            and ("card1" not in scene or _runner.valid_card_export("reference", args.id, args.card_build))
            and all((output / name).is_file() for name in ("run-1.state", "run-2.state", "run-1.ppm"))
            and (args.bios or not expected or hashlib.sha256((output / "run-1.ppm").read_bytes()).hexdigest() == expected)):
        print(f"reference_{args.id}: cached")
        return 0
    # Remove stale captures before either run (failed invocations cannot reuse evidence).
    (output / "state.sha256").unlink(missing_ok=True)
    for index in (1, 2):
        (output / f"run-{index}.ppm").unlink(missing_ok=True)
        (output / f"run-{index}.state").unlink(missing_ok=True)
    with ThreadPoolExecutor(max_workers=2) as executor:
        runs = [executor.submit(run_once, emulator, cues[0].resolve(), bios, scene, symbols,
                                output, index, args.timeout, args.max_vblanks) for index in (1, 2)]
        hashes = [run.result() for run in runs]
    if "card1" in scene:
        card_paths = [_runner.final_card("reference", args.id, args.card_build).parent / f"run-{index}" / "card1.mcd" for index in (1, 2)]
        if card_paths[0].read_bytes() != card_paths[1].read_bytes():
            raise ValueError("non-deterministic reference cards")
        for index in (1, 2):
            _runner.export_card(scene, "reference", index, [card_paths[index-1]], args.card_build)
    if hashes[0] != hashes[1]:
        raise ValueError(f"non-deterministic captures: {hashes[0]} != {hashes[1]}")
    if args.state:
        first, second = output / "run-1.state", output / "run-2.state"
        if first.read_bytes() != second.read_bytes():
            raise ValueError(f"non-deterministic state traces: {first} != {second}")
        records = _runner.read_state(first, scene["_state_vars"], scene["capture_vblank"],
                                     scene.get("checkpoints"), scene.get("window", 0))
        calls = sum(row[1] == "rand" for row in records)
        if scene.get("expect_rand") and calls < 16:
            raise ValueError(f"expected at least 16 rand calls, got {calls}")
    if args.update:
        source = scene_path.read_text()
        line = f'reference_sha256 = "{hashes[0]}"'
        source = re.sub(r'^reference_sha256\s*=.*$', line, source, flags=re.M) if expected else source.rstrip() + "\n" + line + "\n"
        scene_path.write_text(source)
    elif expected and not args.bios and hashes[0] != expected:
        raise ValueError(f"reference hash mismatch: actual={hashes[0]} expected={expected}")
    if args.bios:
        print(f"owner BIOS: OpenBIOS hash={expected or 'unset'}; captured={hashes[0]}")
    if args.state:
        stamp.write_text(fingerprint + "\n")
    print(f"reference_{args.id}: OK sha256={hashes[0]}" + (" (unpinned)" if not expected and not args.update else ""))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print(f"reference: {error}", file=sys.stderr)
        sys.exit(1)

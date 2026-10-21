#!/usr/bin/env python3
"""Run a native scene twice; optionally compare RGB555 against its pinned reference."""
import argparse
import hashlib
import importlib.util
import json
import math
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tomllib

ROOT = Path(__file__).resolve().parents[3]
OPENBIOS = Path("/usr/local/share/pcsx-redux/openbios.bin")
_audio_spec = importlib.util.spec_from_file_location("scene_audio", Path(__file__).with_name("audio.py"))
audio = importlib.util.module_from_spec(_audio_spec)
_audio_spec.loader.exec_module(audio)


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


def validate_scene(scene):
    if type(scene.get("capture_vblank")) is not int or not 0 < scene["capture_vblank"] <= 0xffffffff:
        raise ValueError("capture_vblank must be a positive integer")
    if scene.get("inputs_from", "frame0") not in ("frame0", "mark"):
        raise ValueError("inputs_from must be frame0 or mark")
    if type(scene.get("expect_rand", False)) is not bool:
        raise ValueError("expect_rand must be boolean")
    if not isinstance(scene.get("inputs", []), list):
        raise ValueError("inputs must be an array of tables")
    checkpoints = scene.get("checkpoints", [])
    if not isinstance(checkpoints, list) or any(not isinstance(cp, str) or not re.fullmatch(
            r"[A-Za-z_][\w.$]*(?:#[1-9][0-9]*)?", cp) for cp in checkpoints) or len(set(checkpoints)) != len(checkpoints):
        raise ValueError("checkpoints must be unique symbol or symbol#n strings")
    if "checkpoints" in scene and not checkpoints:
        raise ValueError("empty checkpoints")
    if type(scene.get("window", 0)) is not int or not 0 <= scene.get("window", 0) <= 0xffffffff:
        raise ValueError("window must be a nonnegative integer")
    if any("#" in cp and int(cp.rsplit("#", 1)[1]) > 0xffffffff for cp in checkpoints):
        raise ValueError("checkpoint occurrence exceeds uint32")
    armed = scene.get("checkpoint_after", {})
    if (not isinstance(armed, dict) or any(
            label not in checkpoints or after not in checkpoints
            or checkpoints.index(after) >= checkpoints.index(label)
            for label, after in armed.items())):
        raise ValueError("checkpoint_after must name an earlier checkpoint")
    previous = (-1, -1)
    for event in scene.get("inputs", []):
        keys = {"after", "vblank", "buttons"} if checkpoints else {"vblank", "buttons"}
        if (not isinstance(event, dict) or set(event) != keys
                or type(event["vblank"]) is not int or not 0 <= event["vblank"] <= 0xffffffff
                or not isinstance(event["buttons"], list)
                or any(type(b) is not int or not 0 <= b <= 15 for b in event["buttons"])):
            raise ValueError("inputs require increasing vblank and buttons (PS1 bit indices 0..15)")
        if checkpoints and event["after"] not in checkpoints:
            raise ValueError(f"unknown input checkpoint: {event['after']}")
        position = (checkpoints.index(event["after"]) if checkpoints else 0, event["vblank"])
        if position <= previous:
            raise ValueError("inputs must increase in checkpoint and vblank order")
        previous = position


def checkpoint_symbols(scene, path, binary=None):
    if not scene.get("checkpoints"):
        return []
    source = path.read_text()
    overlay = path.with_name(f"symbols_{scene.get('overlay', '')}.txt")
    if scene.get("overlay") and overlay.is_file(): source += "\n" + overlay.read_text()
    symbols = {name: int(address, 16) for name, address in re.findall(
        r"^([\w.$]+)\s*=\s*(0x[0-9a-fA-F]+);", source, re.M)}
    native = None
    if binary is not None:
        result = subprocess.run(["nm", "-D", "--defined-only", str(binary)], check=True,
                                capture_output=True, text=True)
        native = set(re.findall(r"^[0-9a-fA-F]+\s+[Tt]\s+(\S+)$", result.stdout, re.M))
    result = []
    for label in scene.get("checkpoints", []):
        name, _, occurrence = label.partition("#")
        if name not in symbols:
            raise ValueError(f"missing reference checkpoint symbol: {name}")
        if native is not None and name not in native:
            raise ValueError(f"missing native checkpoint symbol: {name}")
        if native is not None:
            assembly = subprocess.run(["objdump", "-d", "--disassemble=" + name, str(binary)],
                                      check=True, capture_output=True, text=True).stdout
            if not re.search(r"\bcall\w*\s+[^\n]*<__cyg_profile_func_enter>", assembly):
                raise ValueError(f"native checkpoint function is not instrumented: {name}")
        result.append(dict(label=label, address=symbols[name], occurrence=int(occurrence or 1),
                           window=scene.get("window", 0)))
        if label in scene.get("checkpoint_after", {}):
            result[-1]["after"] = scene["checkpoint_after"][label]
    return result


def state_variables(path, symbols_path, binary=None):
    symbols = {name: int(address, 16) for name, address in re.findall(
        r"^([\w.$]+)\s*=\s*(0x[0-9a-fA-F]+);", symbols_path.read_text(), re.M)}
    native = None
    if binary is not None:
        result = subprocess.run(["nm", "-D", "-S", "--defined-only", str(binary)], check=True,
                                capture_output=True, text=True)
        native = {m[2]: (int(m[0], 16), int(m[1], 16)) for m in re.findall(
            r"^([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+[A-Za-z]\s+(\S+)$", result.stdout, re.M)}
    variables = []
    seen = set()
    for line in path.read_text().splitlines():
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        match = re.fullmatch(r"([\w.$]+)\s+(u8|s8|u16|s16|u32|s32|ptr)(?:\[([0-9]+)\])?", line)
        if not match:
            raise ValueError(f"invalid state variable declaration: {line}")
        name, kind, count = match.groups()
        count = int(count or 1)
        width = 4 if kind == "ptr" else int(kind[1:]) // 8
        if count <= 0:
            raise ValueError(f"invalid state variable count: {name}")
        if name in seen:
            raise ValueError(f"duplicate state variable: {name}")
        seen.add(name)
        if name not in symbols:
            raise ValueError(f"missing reference symbol: {name}")
        if not 0x80010000 <= symbols[name] < 0x80200000:
            raise ValueError(f"state variable is not game RAM: {name}")
        if symbols[name] + count * width > 0x80200000:
            raise ValueError(f"state variable exceeds game RAM: {name}")
        if native is not None:
            if name not in native:
                raise ValueError(f"missing native symbol: {name}")
            if count * width > native[name][1]:
                raise ValueError(f"state variable exceeds native symbol: {name}")
        variables.append(dict(name=name, type=kind, count=count, address=symbols[name], width=width))
    if not variables:
        raise ValueError("empty state vars")
    return variables


def bios_fingerprint(path=None):
    try:
        return hashlib.sha256((OPENBIOS if path is None else path).read_bytes()).hexdigest()
    except FileNotFoundError:
        return "openbios"


def state_fingerprint(scene, bios_identity=None):
    if bios_identity is None:
        bios_identity = bios_fingerprint()
    definition = {key: scene.get(key) for key in ("mark", "capture_vblank", "inputs", "inputs_from", "expect_rand", "checkpoints", "window")}
    if "checkpoint_after" in scene:
        definition["checkpoint_after"] = scene["checkpoint_after"]
    content = json.dumps([definition, bios_identity], sort_keys=True).encode()
    for name in ("tools/port/scenes/state_vars.txt", "config/us/symbols.txt",
                 "tools/port/reference/capture.lua", "tools/port/reference/capture.py",
                 "tools/port/scenes/run.py"):
        content += (ROOT / name).read_bytes()
    return hashlib.sha256(content).hexdigest()


def read_state(path, variables, checkpoint, checkpoints=None, window=0):
    if checkpoints:
        return read_checkpoint_state(path, variables, checkpoints, window)
    records = []
    expected = [f"{v['name']}[{i}]" for v in variables for i in range(v['count'])]
    types = {f"{v['name']}[{i}]": v["type"] for v in variables for i in range(v["count"])}
    samples = {}
    rand_index = 0
    previous = 0
    for line in path.read_text().splitlines():
        fields = line.split()
        if len(fields) != 4 or not fields[0].isdigit():
            raise ValueError(f"invalid state trace: {path}: {line}")
        k, kind, name, value = int(fields[0]), *fields[1:]
        if not previous <= k <= checkpoint:
            raise ValueError(f"invalid VBlank in state trace: {path}: {k}")
        previous = k
        if kind == "state":
            if name not in types:
                raise ValueError(f"unknown variable in state trace: {name}")
            kind_type = types[name]
            if kind_type == "ptr":
                if not re.fullmatch(r"0|0x80[0-1][0-9a-f]{5}|[\w.$]+\+[0-9]+", value):
                    raise ValueError(f"invalid normalized pointer: {value}")
            else:
                width = int(kind_type[1:])
                low = -(1 << (width - 1)) if kind_type[0] == "s" else 0
                high = (1 << (width - (kind_type[0] == "s"))) - 1
                if not re.fullmatch(r"-?[0-9]+", value) or not low <= int(value) <= high:
                    raise ValueError(f"invalid {kind_type} value: {value}")
            samples.setdefault(k, []).append(name)
        elif kind == "rand" and name == str(rand_index) and value.isdigit() and 0 <= int(value) <= 32767:
            rand_index += 1
        else:
            raise ValueError(f"invalid state trace record: {line}")
        records.append((k, kind, name, value))
    if set(samples) != set(range(1, checkpoint + 1)) or any(names != expected for names in samples.values()):
        raise ValueError(f"incomplete state trace: {path}")
    return records


def read_checkpoint_state(path, variables, checkpoints, window):
    # Reuse the legacy value/type validator on each complete sample, including offset zero.
    rows = path.read_text().splitlines()
    samples, calls, origins = {}, [], {}
    last_time = -1
    current_order = -1
    for line in rows:
        fields = line.split()
        if len(fields) == 4 and fields[1] == "rand":
            k, _, index, value = fields
            if (not k.isdigit() or int(k) < last_time or index != str(len(calls))
                    or not value.isdigit() or not 0 <= int(value) <= 32767):
                raise ValueError(f"invalid rand record: {line}")
            last_time = int(k)
            calls.append((int(k), "rand", index, value))
            continue
        if (len(fields) != 6 or fields[0] not in checkpoints or fields[3] != "state"
                or not fields[1].isdigit() or not fields[2].isdigit()):
            raise ValueError(f"invalid checkpoint state trace: {line}")
        label, relative, absolute, _, name, value = fields
        relative, absolute = int(relative), int(absolute)
        if not 0 <= relative <= window or absolute < last_time or absolute < relative:
            raise ValueError(f"invalid checkpoint VBlank: {line}")
        if label not in origins:
            if relative or checkpoints.index(label) != current_order + 1:
                raise ValueError(f"checkpoint out of order: {label}")
            current_order += 1
            origins[label] = absolute
        if origins[label] != absolute - relative:
            raise ValueError(f"inconsistent checkpoint origin: {label}")
        last_time = absolute
        samples.setdefault((label, relative), []).append((name, value, absolute))
    expected = [(f"{v['name']}[{i}]", v['type']) for v in variables for i in range(v['count'])]
    if set(samples) != {(cp, k) for cp in checkpoints for k in range(window + 1)}:
        raise ValueError(f"incomplete checkpoint state trace: {path}")
    records = []
    for cp in checkpoints:
        for k in range(window + 1):
            sample = samples[cp, k]
            if [v[0] for v in sample] != [v[0] for v in expected]:
                raise ValueError(f"incomplete checkpoint variables: {cp}+{k}")
            for (name, value, absolute), (_, kind) in zip(sample, expected):
                if kind == "ptr":
                    valid = re.fullmatch(r"0|0x80[0-1][0-9a-f]{5}|[\w.$]+\+[0-9]+", value)
                else:
                    bits = int(kind[1:]); signed = kind[0] == "s"
                    valid = re.fullmatch(r"-?[0-9]+", value) and (-(1 << (bits-1)) if signed else 0) <= int(value) < (1 << (bits-signed))
                if not valid:
                    raise ValueError(f"invalid {kind} value: {value}")
                records.append((absolute, "state", (cp, k, name), value))
    return records + calls


def compare_state(port, reference, variables, checkpoint, expect_rand=False, checkpoints=None, window=0):
    left = read_state(port, variables, checkpoint, checkpoints, window)
    right = read_state(reference, variables, checkpoint, checkpoints, window)
    from itertools import zip_longest
    differences = []
    states = [{(name if checkpoints else (k, name)): (k, value) for k, kind, name, value in records if kind == "state"}
              for records in (left, right)]
    for key, (k, value) in states[0].items():
        other_k, other = states[1][key]
        if value != other:
            if checkpoints:
                cp, relative, name = key
                details = f"checkpoint {cp}+{relative}: {name} (port VBlank {k}, reference VBlank {other_k})"
            else:
                _, details = key
            differences.append((k, details, value, other))
    calls_by_side = [[item for item in records if item[1] == "rand"] for records in (left, right)]
    for index, (a, b) in enumerate(zip_longest(*calls_by_side)):
        if a is None or b is None or a[3] != b[3]:
            k = min(x[0] for x in (a, b) if x)
            details = f"rand call {index}"
            details += f" (port VBlank {a[0] if a else '<missing>'}, reference VBlank {b[0] if b else '<missing>'})"
            differences.append((k, details, a[3] if a else "<missing>", b[3] if b else "<missing>"))
    if differences:
        k, name, value, other = min(differences, key=lambda item: item[0])
        raise ValueError(f"first divergence at VBlank {k}: {name} port={value} reference={other}")
    calls = sum(item[1] == "rand" for item in left)
    if expect_rand and calls < 16:
        raise ValueError(f"expect_rand requires at least 16 rand calls, got {calls}")
    scope = f"{len(checkpoints)} checkpoints, window {window}" if checkpoints else f"{checkpoint} VBlanks"
    print(f"state match: {scope}, {len(variables)} variables, {calls} rand calls")
    return 0


def run_audio(args, scene, build, checkpoints, pack):
    audio.validate_segments(scene)
    for segment in scene["audio"]:
        if segment.get("stop_on"):
            checkpoint_symbols(dict(checkpoints=segment["stop_on"]), ROOT / "config/us/symbols.txt", build / "dw1")
    report = ROOT / "build/port" / args.build / "scenes" / args.id / "audio.json"
    report.unlink(missing_ok=True)
    stamp = ROOT / "build/reference" / args.id / "audio.json"
    if not stamp.is_file():
        print(f"scene {args.id}: no deterministic audio reference; skipped")
        return 77
    record = json.loads(stamp.read_text())
    if record.get("scene") != dict(scene, resolved_checkpoints=checkpoints):
        raise ValueError("stale audio reference scene; run capture.py --audio")
    identity_spec = importlib.util.spec_from_file_location("reference_audio", Path(__file__).parents[1] / "reference/audio.py")
    reference_audio = importlib.util.module_from_spec(identity_spec)
    identity_spec.loader.exec_module(reference_audio)
    emulator = shutil.which("pcsx-redux")
    try:
        identity = reference_audio.fingerprint(record["scene"], Path(record["bios"]), emulator, ROOT) if emulator else None
    except (KeyError, OSError):
        identity = None
    if not identity or identity != record.get("fingerprint"):
        raise ValueError("stale audio reference identity (emulator/BIOS/adapter/symbols); run capture.py --audio")
    reference = audio.external_output(record["output"], ROOT)
    hashes = record.get("hashes", {})
    required = {f"run-{run}-{segment}.{suffix}" for run in (1, 2)
                for segment in range(1,len(scene["audio"])+1) for suffix in (("spu", "wav", "xa.wav") if scene.get("xa") else ("spu", "wav"))}
    if scene.get("movie"):
        for index in (1,2): required.update(reference_audio.movie.evidence(reference / f"movie-{index}"))
    if set(hashes) != required:
        raise ValueError("incomplete audio reference manifest")
    for name, expected in hashes.items():
        path = reference / name
        if (not path.resolve().is_relative_to(reference) or not path.is_file()
                or hashlib.sha256(path.read_bytes()).hexdigest() != expected):
            raise ValueError("missing/stale audio reference evidence; run capture.py --audio")
    output = audio.external_output(Path(os.environ.get("DW1_AUDIO_DIR", "/tmp/dw1-audio-port")) /
                                   args.build / args.id, ROOT)
    if output == reference or output.is_relative_to(reference) or reference.is_relative_to(output):
        raise ValueError("audio output and reference directories must be disjoint")
    if any(character.isspace() for character in str(output)):
        raise ValueError("audio output directory cannot contain whitespace")
    output.mkdir(parents=True, exist_ok=True)
    inputs, cp_file = output / "inputs.txt", output / "checkpoints.txt"
    inputs.write_text("".join(f"{e['after']} {e['vblank']} {sum(1 << b for b in set(e['buttons'])):04x}\n"
                              for e in scene.get("inputs", [])))
    cp_file.write_text("".join(f"{cp['label']} 0 {cp.get('after', '-')}\n" for cp in checkpoints))
    all_hashes = []
    for index in (1, 2):
        declarations = output / f"segments-{index}.txt"
        declarations.write_text("".join(
            f"{seg['start']} {seg.get('finish', '-')} {seg.get('vblanks', 0)} "
            f"{output}/run-{index}-{i}.spu {output}/run-{index}-{i}.wav "
            f"{','.join(seg.get('stop_on', [])) or '-'} {seg.get('anchor', '-')}\n"
            for i, seg in enumerate(scene["audio"], 1)))
        paths = [output / f"run-{index}-{i}.{suffix}" for i in range(1,len(scene["audio"])+1) for suffix in (("spu", "wav", "xa.wav") if scene.get("xa") else ("spu", "wav"))]
        for path in paths:
            path.unlink(missing_ok=True)
        environment = dict(os.environ)
        if scene.get("xa"):
            environment["DW1_XA_WAV"] = str(output / f"run-{index}-1.xa.wav")
        if scene.get("movie"):
            directory=output / f"movie-{index}"
            if directory.exists(): shutil.rmtree(directory)
            directory.mkdir()
            environment["DW1_MOVIE_CAPTURE"] = str(directory)
        if args.build == "native-asan":
            environment["ASAN_OPTIONS"] = environment.get("ASAN_OPTIONS", "") + ":detect_stack_use_after_return=0"
        command = [str(build / "dw1"), "--headless", "--pack", str(pack), "--frames", "40000",
                   "--capture-mark", scene["mark"], "--inputs-from", "mark", "--inputs", str(inputs),
                   "--checkpoints", str(cp_file), "--audio-segments", str(declarations)]
        result = subprocess.run(command, cwd=build, env=environment, capture_output=True, timeout=3500 if scene.get("movie") else 1800)
        log = result.stdout + result.stderr
        (output / f"run-{index}.log").write_bytes(log)
        if result.returncode or re.search(rb"AddressSanitizer|runtime error", log):
            raise ValueError(f"audio run {index} failed: exit {result.returncode}; {output}/run-{index}.log")
        all_hashes.append([hashlib.sha256(path.read_bytes()).hexdigest() for path in paths])
    if all_hashes[0] != all_hashes[1]:
        raise ValueError("non-deterministic native SPU trace/PCM")
    metrics, failures = [], []
    video = None
    if scene.get("movie"):
        reference_audio.movie.deterministic(output / "movie-1", output / "movie-2")
        video = reference_audio.movie.compare(output / "movie-1",reference / "movie-1",scene,output / "diff")
        failures.extend(video["failures"])
        print(f'movie: port={video["port_frames"]}, reference={video["reference_frames"]}, A/V={video["av"]}, failed checks={len(video["failures"])}')
    for i, segment in enumerate(scene["audio"], 1):
        actual, expected = output / f"run-1-{i}.spu", reference / f"run-1-{i}.spu"
        content = audio.diagnostic(audio.compare_voice_content, actual, expected,
                                   music=i == 2, stop_on=segment.get("stop_on", []), all_registers=scene.get("xa", False))
        trace = audio.trace_diagnostics(actual, expected)
        # Decision 4 defers fidelity metrics, not evidence-format validation.
        for side, directory, trace_path in (("port", output, actual), ("reference", reference, expected)):
            frames = len(audio.read_pcm(directory / f"run-1-{i}.wav")) // 2
            end = audio.trace_data(trace_path)[1]
            if not end or abs(frames - end["sample"]) > 1:
                raise ValueError(f"segment {i}: {side} WAV frame count {frames} does not match trace interval {end}")
        pcm = audio.diagnostic(audio.compare_pcm, output / f"run-1-{i}.wav",
                               reference / f"run-1-{i}.wav", diagnostic_only=True)
        xa_pcm = None
        if scene.get("xa"):
            xa_pcm = audio.diagnostic(audio.compare_pcm, output / f"run-1-{i}.xa.wav",
                                      reference / f"run-1-{i}.xa.wav", whole_window=True)
            # The Start scenario gates exit timing/return; its short XA prefix
            # is diagnostic and need not contain the existing 5 s SNR window.
            if xa_pcm["status"] != "match" and not (scene.get("movie") and scene["expected_return"] == 0):
                failures.append(f"XA: {xa_pcm['error']}")
        bounds = dict(port=audio.trace_data(actual)[1], reference=audio.trace_data(expected)[1])
        metrics.append(dict(segment=i, content=content, capture_bounds=bounds, rule_b=trace, pcm=pcm, xa=xa_pcm))
        if content["status"] != "match":
            failures.append(f"segment {i}: {content['error']}")
        print(f"audio segment {i}: content={content}; capture bounds={bounds}; rule B (non-blocking)={trace}; "
              f"PCM (non-blocking)={pcm}; XA={xa_pcm}")
    # Only hashes/metrics go into build/; PCM and register evidence stay external.
    report.parent.mkdir(parents=True, exist_ok=True)
    def finite_metrics(value):
        if isinstance(value, float) and not math.isfinite(value):
            return "+inf" if value > 0 else "-inf"
        if isinstance(value, dict):
            return {key: finite_metrics(item) for key, item in value.items()}
        if isinstance(value, list):
            return [finite_metrics(item) for item in value]
        return value
    report.write_text(json.dumps(finite_metrics(dict(hashes=all_hashes[0], segments=metrics, video=video)),
                                 indent=2, allow_nan=False) + "\n")
    if failures:
        raise ValueError("; ".join(failures[:8]) + (f"; {len(failures)} failed checks (see {report})" if len(failures)>8 else ""))
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("id", choices=sorted(p.stem for p in (ROOT / "tools/port/scenes").glob("*.toml")))
    parser.add_argument("--build", required=True, choices=["native-headless", "native-asan"])
    parser.add_argument("--pack", type=Path)
    parser.add_argument("--compare", action="store_true")
    parser.add_argument("--compare-audio", action="store_true")
    parser.add_argument("--state", action="store_true")
    args = parser.parse_args()
    if args.compare_audio and (args.compare or args.state):
        raise ValueError("--compare-audio cannot be combined with --compare or --state")
    build = ROOT / "build/port" / args.build
    output = build / "scenes" / args.id
    if args.compare:
        (output / "diff.ppm").unlink(missing_ok=True)
    scene = tomllib.loads((ROOT / "tools/port/scenes" / f"{args.id}.toml").read_text())
    validate_scene(scene)
    if args.compare_audio:
        audio.validate_segments(scene)
    frame = validate_comparison(scene, args.id) if args.compare and not scene.get("movie") else None
    vars_path = ROOT / "tools/port/scenes/state_vars.txt"
    checkpoints = checkpoint_symbols(scene, ROOT / "config/us/symbols.txt", build / "dw1")
    variables = state_variables(vars_path, ROOT / "config/us/symbols.txt", build / "dw1") if args.state else []
    reference_state = reference_path(args.id).with_suffix(".state")
    stamp = reference_state.parent / "state.sha256"
    if args.state and (not reference_state.is_file() or not stamp.is_file() or
                       stamp.read_text().strip() != state_fingerprint(scene)):
        command = [sys.executable, "-I", "-B", str(ROOT / "tools/port/reference/capture.py"), args.id,
                   "--state", "--max-vblanks", str(max(40000, scene["capture_vblank"] + 9000))]
        if checkpoints:
            command += ["--timeout", "1800"]
        status = subprocess.run(command).returncode
        if status: return status
    pack = args.pack.resolve() if args.pack else build / "us.pack"
    if not pack.is_file():
        if args.pack is not None:
            raise ValueError(f"Pack file does not exist: {pack}")
        disc = os.environ.get("DW1_DISC_DIR")
        if not disc:
            print(f"scene {args.id}: no Pack or DW1_DISC_DIR; skipped")
            return 77
        cues = sorted(Path(disc).glob("*.cue"))
        if not cues:
            print(f"scene {args.id}: no .cue in {disc}; skipped")
            return 77
        subprocess.run([str(build / "dw1-pack"), "build", str(cues[0]),
                        str(ROOT / "tools/port/manifest/us.tsv"), str(pack)], check=True)
    if args.compare_audio or scene.get("movie"):
        return run_audio(args, scene, build, checkpoints, pack)
    if scene.get("audio"):
        raise ValueError("audio scenes require --compare-audio")
    output.mkdir(parents=True, exist_ok=True)
    inputs = output / "inputs.txt"
    inputs.write_text("".join((e['after'] + " " if checkpoints else "") + f"{e['vblank']} {sum(1 << b for b in set(e['buttons'])):04x}\n" for e in scene.get("inputs", [])))
    checkpoint_file = output / "checkpoints.txt"
    if checkpoints:
        checkpoint_file.write_text("".join(f"{cp['label']} {cp['window']} {cp.get('after', '-')}\n"
                                           for cp in checkpoints))
    results = []
    for index in (1, 2):
        capture = output / f"run-{index}.ppm"
        trace = output / f"run-{index}.trace"
        clock = output / f"run-{index}.clock"
        state = output / f"run-{index}.state"
        for path in (capture, trace, clock, state):
            path.unlink(missing_ok=True)
        env = dict(os.environ, DW1_TRACE=str(trace))
        if args.build == "native-asan":
            # Same as the native-asan test preset: the fake stack would move game-stack
            # primitives out of the OT window (AD-5).
            env["ASAN_OPTIONS"] = env.get("ASAN_OPTIONS", "") + ":detect_stack_use_after_return=0"
        command = [str(build / "dw1"), "--headless", "--pack", str(pack),
                   "--frames", "40000", "--capture-mark", scene["mark"],
                   "--capture-vblank", str(scene["capture_vblank"]),
                   "--capture", str(capture), "--clock-trace", str(clock)]
        command += ["--inputs", str(inputs), "--inputs-from", scene.get("inputs_from", "frame0")]
        if checkpoints:
            command += ["--checkpoints", str(checkpoint_file)]
        if args.state:
            command += ["--state-vars", str(vars_path), "--state-trace", str(state)]
        run = subprocess.run(command, cwd=build, env=env, capture_output=True, timeout=600)
        log = run.stdout + run.stderr
        (output / f"run-{index}.log").write_bytes(log)
        if run.returncode or re.search(rb"AddressSanitizer|runtime error", log):
            raise ValueError(f"run {index}: exit {run.returncode}\n{log.decode(errors='replace')}")
        if not re.search(rf"^\d+ mark {re.escape(scene['mark'])}$", trace.read_text(), re.M):
            raise ValueError(f"run {index}: missing scene mark")
        colors = ppm_colors(capture)
        if colors <= 16 and not args.state:
            raise ValueError(f"run {index}: only {colors} colors")
        hashes = tuple(hashlib.sha256(path.read_bytes()).hexdigest() for path in ((capture, clock, state) if args.state else (capture, clock)))
        if not clock.stat().st_size:
            raise ValueError("empty clock trace")
        results.append(hashes)
        print(f"{capture}: sha256={hashes[0]} colors={colors}")
        print(f"{clock}: sha256={hashes[1]}")
    if results[0] != results[1]:
        raise ValueError("capture, clock or state trace differs between runs")
    if args.state:
        ref = reference_path(args.id)
        if ref.is_file():
            print("informative frame comparison:")
            try:
                compare_capture(output / "run-1.ppm", dict(scene, reference_sha256=hashlib.sha256(ref.read_bytes()).hexdigest()), args.id, read_ppm(ref))
            except ValueError as error:
                print(error)
        compare_state(output / "run-1.state", reference_state, variables,
                      scene["capture_vblank"], scene.get("expect_rand", False), scene.get("checkpoints"), scene.get("window", 0))
    if args.compare and compare_capture(output / "run-1.ppm", scene, args.id, frame):
        return 1
    print(f"scene_{args.id}: {args.build}: OK (two identical captures and clock traces)")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print(f"scene: {error}", file=sys.stderr)
        sys.exit(1)

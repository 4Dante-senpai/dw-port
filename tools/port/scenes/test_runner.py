#!/usr/bin/env python3
"""L1: the scene runner rejects failed, blank, missing, or nondeterministic evidence."""
from contextlib import redirect_stdout
import importlib.util
import io
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from unittest.mock import patch

spec = importlib.util.spec_from_file_location("scene_runner", Path(__file__).with_name("run.py"))
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)
with tempfile.TemporaryDirectory() as directory:
    root = Path(directory)
    scenes = root / "tools/port/scenes"
    scenes.mkdir(parents=True)
    (scenes / "title.toml").write_bytes(Path(__file__).with_name("title.toml").read_bytes())
    build = root / "build/port/native-headless"
    build.mkdir(parents=True)
    with patch.object(runner, "ROOT", root), patch.object(sys, "argv", ["run.py", "title", "--build", "native-headless"]), patch.dict(os.environ, {}, clear=True), redirect_stdout(io.StringIO()):
        assert runner.main() == 77
        missing_pack = root / "typo.pack"
        (root / "disc.cue").touch()
        for disc_env in ({}, {"DW1_DISC_DIR": str(root)}):
            with patch.object(sys, "argv", sys.argv + ["--pack", str(missing_pack)]), patch.dict(os.environ, disc_env, clear=True), patch.object(runner.subprocess, "run") as process:
                try:
                    runner.main()
                except ValueError as error:
                    assert str(missing_pack) in str(error), error
                else:
                    raise AssertionError("missing explicit Pack must fail")
                process.assert_not_called()
                assert not missing_pack.exists()
        (build / "us.pack").touch()
        for mode in ("ok", "exit", "asan", "ubsan", "mark", "missing", "blank", "truncated", "capture-diff", "clock-diff"):
            calls = []
            def host(command, **kwargs):
                def flag(name):
                    return command[command.index(name) + 1]
                calls.append(command)
                index = len(calls)
                assert flag("--capture-mark") == "runLandingScreen"
                assert flag("--capture-vblank") == "60"
                Path(kwargs["env"]["DW1_TRACE"]).write_text("1 mark runLandingScreen\n" if mode != "mark" else "")
                Path(flag("--clock-trace")).write_text(f"{index if mode == 'clock-diff' else 1} -1\n")
                colors = 16 if mode == "blank" else 17
                pixels = b"".join(bytes([i, 0, index if mode == "capture-diff" else 0]) for i in range(colors))
                if mode == "truncated":
                    pixels = pixels[:-1]
                if mode != "missing":
                    Path(flag("--capture")).write_bytes(f"P6\n{colors} 1\n255\n".encode() + pixels)
                diagnostic = {"asan": b"AddressSanitizer", "ubsan": b"runtime error"}.get(mode, b"")
                return subprocess.CompletedProcess(command, 1 if mode == "exit" else 0, b"", diagnostic)
            with patch.object(runner.subprocess, "run", side_effect=host):
                try:
                    result = runner.main()
                except (ValueError, OSError):
                    assert mode != "ok", mode
                else:
                    assert mode == "ok" and result == 0 and len(calls) == 2, mode
print("scene_runner: skip 77, missing explicit Pack, success, failed exit, sanitizers, missing mark/capture, <=16 colors, malformed PPM, divergent capture/clock: OK")

# Comparison matrix: execute the real runner with only the host process replaced.
with tempfile.TemporaryDirectory() as directory:
    root = Path(directory)
    scenes = root / "tools/port/scenes"
    scenes.mkdir(parents=True)
    build = root / "build/port/native-headless"
    build.mkdir(parents=True)
    (build / "us.pack").touch()
    reference = root / "build/reference/title/run-1.ppm"
    reference.parent.mkdir(parents=True)
    pixels = bytes(c for i in range(32) for c in (i * 8, i * 8, 0))
    ppm = b"P6\n8 4\n255\n" + pixels
    import hashlib
    digest = hashlib.sha256(ppm).hexdigest()
    base = 'mark = "runLandingScreen"\ncapture_vblank = 60\n'
    region = '\n[[exceptions]]\nregion = [1, 1, 3, 2]\nreason = "synthetic allowance"\n'
    for mode in ("equal", "rgb555", "different", "covered", "partial", "no-hash", "no-reference",
                 "invalid-region", "no-reason", "dimensions", "stale-reference", "overlap", "external", "no-reference-equal", "missing-pack"):
        reference.write_bytes(ppm)
        text = base + (f'reference_sha256 = "{digest}"\n' if mode != "no-hash" else '')
        if mode in ("covered", "partial", "overlap"):
            text += region
        if mode == "overlap":
            text += region.replace("synthetic allowance", "overlapping allowance")
        if mode == "invalid-region":
            text += region.replace('3, 2', '8, 2')
        if mode == "no-reason":
            text += region.replace('synthetic allowance', ' ')
        (scenes / "title.toml").write_text(text)
        changed = bytearray(pixels)
        if mode in ("different", "covered", "partial", "no-reference", "overlap"):
            for x, y in ((1, 1), (3, 2)):
                changed[(y * 8 + x) * 3] ^= 8
        if mode == "partial":
            changed[0] ^= 8
        if mode == "rgb555":
            changed = bytearray(c | 7 for c in changed)
        capture = b"P6\n8 4\n255\n" + changed
        if mode == "dimensions":
            capture = b"P6\n4 8\n255\n" + changed
        if mode in ("no-reference", "no-reference-equal"):
            reference.unlink()
        if mode == "stale-reference":
            reference.write_bytes(ppm[:-1] + bytes([ppm[-1] ^ 8]))
        calls = []
        def host(command, **kwargs):
            calls.append(command)
            def flag(name):
                return Path(command[command.index(name) + 1])
            Path(kwargs["env"]["DW1_TRACE"]).write_text("1 mark runLandingScreen\n")
            flag("--clock-trace").write_text("1 -1\n")
            flag("--capture").write_bytes(capture)
            return subprocess.CompletedProcess(command, 0, b"", b"")
        output = io.StringIO()
        environment = {}
        if mode == "external":
            external = root / "external/title"
            external.mkdir(parents=True)
            reference.rename(external / "run-1.ppm")
            environment["DW1_REFERENCE_DIR"] = str(external.parent)
        diff_path = build / "scenes/title/diff.ppm"
        diff_path.parent.mkdir(parents=True, exist_ok=True)
        diff_path.write_bytes(b"stale diff")
        argv = ["run.py", "title", "--build", "native-headless", "--compare"]
        if mode == "missing-pack":
            argv += ["--pack", str(root / "missing.pack")]
        must_raise = ("no-hash", "no-reference", "invalid-region", "no-reason",
                      "dimensions", "stale-reference", "missing-pack")
        with patch.object(runner, "ROOT", root), patch.dict(os.environ, environment, clear=True), patch.object(
                sys, "argv", argv), patch.object(
                runner.subprocess, "run", side_effect=host), redirect_stdout(output):
            try:
                result = runner.main()
            except ValueError as error:
                assert mode in must_raise, (mode, error)
                assert not diff_path.exists(), mode
                if mode != "no-reference" and mode != "dimensions":
                    assert not calls, mode
                if mode == "no-reference":
                    assert digest in str(error) and hashlib.sha256(capture).hexdigest() in str(error) and "capture.py" in str(error)
            else:
                assert mode not in must_raise, f"{mode} must raise"
                assert len(calls) == 2
                assert result == (1 if mode in ("different", "partial") else 0), mode
                report = output.getvalue()
                if mode == "different":
                    assert "2 pixels differ, box 1..3 1..2" in report, report
                if mode == "partial":
                    assert "1 pixels differ, box 0..0 0..0" in report, report
                if mode in ("covered", "partial", "overlap"):
                    assert "synthetic allowance (2 px)" in report, report
                if mode == "overlap":
                    assert "exception [1, 1, 3, 2]: synthetic allowance (2 px)" in report, report
                    assert "exception [1, 1, 3, 2]: overlapping allowance (0 px)" in report, report
                if mode in ("different", "covered", "partial", "overlap"):
                    diff = runner.read_ppm(build / "scenes/title/diff.ppm")
                    assert diff[:2] == (8, 4) and diff[2][27:30] == bytes((255, 0, 255))
                else:
                    assert not (build / "scenes/title/diff.ppm").exists()
print("scene_runner: RGB555 comparison, bounding box, exceptions, missing/stale reference, dimensions and preflight validation: OK")

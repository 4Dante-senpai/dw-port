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

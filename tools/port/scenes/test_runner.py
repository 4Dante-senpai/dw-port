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
from types import SimpleNamespace
from unittest.mock import patch

spec = importlib.util.spec_from_file_location("scene_runner", Path(__file__).with_name("run.py"))
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)
def write_timing(path, mark, checkpoints=()):
    import json
    costs = dict.fromkeys(runner.timing.SOURCES, 0)
    rows = [dict(event='frame0', label='frame0', cycle=0, vblank=0, costs=costs),
            dict(event='mark', label=mark, cycle=0, vblank=0, costs=costs)]
    rows += [dict(event='checkpoint', label=cp, cycle=0, vblank=0, costs=costs) for cp in checkpoints]
    Path(path).write_text('\n'.join(map(json.dumps, rows)) + '\n')


for preset in ("native-headless", "native"):
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory) / preset
        root.mkdir()
        scenes = root / "tools/port/scenes"
        scenes.mkdir(parents=True)
        (scenes / "title.toml").write_text("\n".join(line for line in Path(__file__).with_name("title.toml").read_text().splitlines() if not line.startswith(("checkpoints =", "window ="))) + "\n")
        if preset == "native":
            import hashlib
            sample = b"P6\n17 1\n255\n" + b"".join(bytes([i, 0, 0]) for i in range(17))
            scene_path = scenes / "title.toml"
            scene_path.write_text(scene_path.read_text().replace(
                "561323665a33d833c89339030f6a8c60e4a395add2cff467d1d47e92dd46260d",
                hashlib.sha256(sample).hexdigest()))
        build = root / "build/port" / preset
        build.mkdir(parents=True)
        with patch.object(runner, "ROOT", root), patch.object(sys, "argv", ["run.py", "title", "--build", preset]), patch.dict(os.environ, {}, clear=True), redirect_stdout(io.StringIO()):
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
            for mode in ("ok", "exit", "asan", "ubsan", "mark", "missing", "blank", "truncated", "capture-diff", "clock-diff", "wrong-hash"):
                calls = []
                def host(command, **kwargs):
                    def flag(name):
                        return command[command.index(name) + 1]
                    assert ("--headless" in command) == (preset != "native")
                    if preset == "native":
                        assert kwargs["env"]["SDL_VIDEODRIVER"] == "offscreen"
                        assert kwargs["env"]["SDL_AUDIODRIVER"] == "dummy"
                    write_timing(kwargs["env"]["DW1_TIMING_TRACE"], "runLandingScreen")
                    calls.append(command)
                    index = len(calls)
                    assert flag("--capture-mark") == "runLandingScreen"
                    assert flag("--capture-vblank") == "60"
                    Path(kwargs["env"]["DW1_TRACE"]).write_text("1 mark runLandingScreen\n" if mode != "mark" else "")
                    Path(flag("--clock-trace")).write_text(f"{index if mode == 'clock-diff' else 1} -1\n")
                    colors = 16 if mode == "blank" else 17
                    pixels = b"".join(bytes([i, 0, index if mode == "capture-diff" else 1 if mode == "wrong-hash" else 0]) for i in range(colors))
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
                        assert (mode == "ok" or (mode == "wrong-hash" and preset != "native")) and result == 0 and len(calls) == 2, mode
print("scene_runner: skip 77, missing explicit Pack, success, failed exit, sanitizers, missing mark/capture, <=16 colors, malformed PPM, divergent capture/clock: OK")

# Audio-only runs never request a display capture, and refuse unverified evidence.
with tempfile.TemporaryDirectory() as directory:
    root = Path(directory) / "repo"
    reference, output = Path(directory) / "reference", Path(directory) / "audio"
    root.mkdir(); reference.mkdir()
    build = root / "build/port/native-headless"
    scene = dict(mark="start", checkpoints=["start", "finish", "music"], inputs=[],
                 audio=[dict(start="start", finish="finish"), dict(start="music", stop_on=["game_event"], anchor="note_on")])
    checkpoints = [dict(label=name) for name in scene["checkpoints"]]
    args = SimpleNamespace(id="title_audio", build="native-headless")
    stamp = root / "build/reference/title_audio/audio.json"
    import hashlib
    import json
    import wave
    def audio_host(command, **kwargs):
        write_timing(kwargs["env"]["DW1_TIMING_TRACE"], "start", scene["checkpoints"])
        assert "--capture" not in command and "--capture-vblank" not in command
        declarations = Path(command[command.index("--audio-segments") + 1])
        for row in declarations.read_text().splitlines():
            start, finish, window, trace, wav, stops, anchor = row.split()
            assert anchor == ("note_on" if start == "music" else "-")
            Path(trace).write_text(f"# dw1-spu-trace-v1\n0 0 000 0001\n# end 441000 {'checkpoint' if finish != '-' else 'event:game_event'}\n")
            with wave.open(wav, "wb") as stream:
                stream.setparams((2, 2, 44100, 0, "NONE", "not compressed"))
                stream.writeframes(b"\x01\0" * (44100 * 10 * 2))
        return subprocess.CompletedProcess(command, 0, b"", b"")
    with patch.object(runner, "ROOT", root), patch.object(runner.shutil, "which", return_value=str(root/"emulator")), patch.object(runner, "checkpoint_symbols", return_value=[]), patch.dict(os.environ, {"DW1_AUDIO_DIR": str(output)}, clear=True), redirect_stdout(io.StringIO()):
        with patch.object(runner.subprocess, "run") as process:
            assert runner.run_audio(args, scene, build, checkpoints, root / "us.pack") == 77
            process.assert_not_called()
        stamp.parent.mkdir(parents=True)
        for run in (1, 2):
            for segment in (1, 2):
                (reference / f"run-{run}-{segment}.spu").write_text(f"# dw1-spu-trace-v1\n0 0 000 0001\n# end 441000 {'checkpoint' if segment == 1 else 'event:game_event'}\n")
                with wave.open(str(reference / f"run-{run}-{segment}.wav"), "wb") as stream:
                    stream.setparams((2, 2, 44100, 0, "NONE", "not compressed"))
                    stream.writeframes(b"\x01\0" * (44100 * 10 * 2))
        for index in (1, 2):
            write_timing(reference / f"run-{index}.timing.jsonl", "start", scene["checkpoints"])
            (reference / f"run-{index}.hardware.jsonl").write_text('{}\n')
        hashes = {path.name: hashlib.sha256(path.read_bytes()).hexdigest() for path in reference.iterdir()}
        identity_spec = importlib.util.spec_from_file_location("reference_audio", Path(runner.__file__).parents[1]/"reference/audio.py")
        identity_module = importlib.util.module_from_spec(identity_spec)
        identity_spec.loader.exec_module(identity_module)
        for name in ("tools/port/reference/capture.py", "tools/port/reference/capture.lua", "tools/port/reference/hardware.lua", "tools/port/reference/audio.py",
                     "tools/port/scenes/audio.py", "tools/port/reference/pcsx-redux-pcm-tap.patch", "bios", "emulator"):
            path = root/name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(name)
        symbols = root/"config/us/symbols.txt"
        symbols.parent.mkdir(parents=True)
        symbols.write_text("VSync = 0x80010000;\nstart = 0x80010004;\nfinish = 0x80010008;\nmusic = 0x8001000c;\ngame_event = 0x80010010;\n")
        definition = dict(scene, resolved_checkpoints=checkpoints)
        stamp.write_text(json.dumps(dict(scene=definition, bios=str(root/"bios"),
                                        fingerprint=identity_module.fingerprint(definition,root/"bios",root/"emulator",root),
                                        output=str(reference), hashes=hashes)))
        with patch.object(runner.subprocess, "run", side_effect=audio_host) as process, patch.object(runner.audio, "alignment", return_value=0):
            assert runner.run_audio(args, scene, build, checkpoints, root / "us.pack") == 0
            assert process.call_count == 2
            assert (build / "scenes/title_audio/audio.json").exists()
        # Content remains the only fidelity gate; low SNR, -inf and rule B
        # mismatch must still be serialized even when the content gate fails.
        original_host = audio_host
        for changed, expected_error in ((False, None), (True, "first voice-content divergence")):
            def diagnostic_host(command, **kwargs):
                result = original_host(command, **kwargs)
                declarations = Path(command[command.index("--audio-segments")+1])
                for row in declarations.read_text().splitlines():
                    _, finish, _, trace, wav, _, _ = row.split()
                    Path(trace).write_text("# dw1-spu-trace-v1\n10 1000 188 0020\n"
                        + f"10 1001 000 {2 if changed else 1:04x}\n# end 441000 {'checkpoint' if finish != '-' else 'event:game_event'}\n")
                    with wave.open(wav, "wb") as stream:
                        stream.setparams((2, 2, 44100, 0, "NONE", "not compressed"))
                        stream.writeframes(b"\0\0" * (44100*10*2))
                return result
            with patch.object(runner.subprocess, "run", side_effect=diagnostic_host), patch.object(runner.audio, "alignment", return_value=0):
                try:
                    result = runner.run_audio(args, scene, build, checkpoints, root/"us.pack")
                except ValueError as error:
                    assert expected_error and expected_error in str(error), error
                else:
                    assert expected_error is None and result == 0
                evidence = json.loads((build/"scenes/title_audio/audio.json").read_text())
                assert evidence["segments"][0]["rule_b"]["rule_b"]["status"] == "different"
                assert not evidence["segments"][0]["pcm"]["metrics"]["meets_40_db"]
                assert evidence["segments"][0]["content"]["status"] == ("different" if changed else "match")
        # Identical content cannot waive the minimum music interval.
        def short_host(command, **kwargs):
            result = original_host(command, **kwargs)
            declarations = Path(command[command.index("--audio-segments")+1])
            music_trace = Path(declarations.read_text().splitlines()[1].split()[3])
            music_trace.write_text(music_trace.read_text().replace("441000", "396899"))
            music_wav = Path(declarations.read_text().splitlines()[1].split()[4])
            with wave.open(str(music_wav), "wb") as stream:
                stream.setparams((2, 2, 44100, 0, "NONE", "not compressed"))
                stream.writeframes(b"\x01\0" * (396899 * 2))
            return result
        with patch.object(runner.subprocess, "run", side_effect=short_host), patch.object(runner.audio, "alignment", return_value=0):
            try:
                runner.run_audio(args, scene, build, checkpoints, root/"us.pack")
            except ValueError as error:
                assert "segment 2: port music" in str(error) and ">= 9 s" in str(error), error
            else:
                raise AssertionError("short music accepted despite matching content")
            evidence = json.loads((build/"scenes/title_audio/audio.json").read_text())
            assert evidence["segments"][0]["content"]["status"] == "match"
            assert evidence["segments"][1]["content"]["status"] == "different"
            assert evidence["segments"][1]["capture_bounds"]["port"] == dict(
                sample=396899, reason="event:game_event")
            assert evidence["segments"][1]["capture_bounds"]["reference"]["sample"] == 441000
        def truncated_host(command, **kwargs):
            result = original_host(command, **kwargs)
            declarations = Path(command[command.index("--audio-segments")+1])
            wav = Path(declarations.read_text().splitlines()[0].split()[4])
            wav.write_bytes(wav.read_bytes()[:-4])
            return result
        with patch.object(runner.subprocess, "run", side_effect=truncated_host):
            try:
                runner.run_audio(args, scene, build, checkpoints, root/"us.pack")
            except ValueError as error:
                assert "truncated WAV" in str(error), error
            else:
                raise AssertionError("malformed PCM accepted as a non-blocking metric")
        for interval_delta in (-2, -1, 1, 2):
            def wrong_interval_host(command, **kwargs):
                result = original_host(command, **kwargs)
                declarations = Path(command[command.index("--audio-segments")+1])
                trace = Path(declarations.read_text().splitlines()[0].split()[3])
                trace.write_text(trace.read_text().replace("441000", str(441000 + interval_delta)))
                return result
            with patch.object(runner.subprocess, "run", side_effect=wrong_interval_host), patch.object(runner.audio, "alignment", return_value=0):
                try:
                    result = runner.run_audio(args, scene, build, checkpoints, root/"us.pack")
                except ValueError as error:
                    assert abs(interval_delta) == 2 and "WAV frame count" in str(error), error
                else:
                    assert abs(interval_delta) == 1 and result == 0, "wrong WAV/trace interval accepted"
        # A valid, hash-verified reference WAV must cover its trace too.
        saved_stamp = stamp.read_bytes()
        saved_wavs = {}
        for run in (1, 2):
            wav = reference/f"run-{run}-1.wav"
            saved_wavs[wav] = wav.read_bytes()
            with wave.open(str(wav), "wb") as stream:
                stream.setparams((2, 2, 44100, 0, "NONE", "not compressed"))
                stream.writeframes(b"\x01\0" * (441002 * 2))
        changed_stamp = json.loads(saved_stamp)
        for wav in saved_wavs:
            changed_stamp["hashes"][wav.name] = hashlib.sha256(wav.read_bytes()).hexdigest()
        stamp.write_text(json.dumps(changed_stamp))
        with patch.object(runner.subprocess, "run", side_effect=original_host):
            try:
                runner.run_audio(args, scene, build, checkpoints, root/"us.pack")
            except ValueError as error:
                assert "reference WAV frame count" in str(error), error
            else:
                raise AssertionError("reference WAV with wrong trace interval accepted")
        for wav, data in saved_wavs.items():
            wav.write_bytes(data)
        stamp.write_bytes(saved_stamp)
        for dependency in (root/"bios", root/"emulator", root/"tools/port/reference/capture.lua", root/"tools/port/reference/hardware.lua", symbols):
            saved = dependency.read_bytes()
            dependency.write_bytes(saved.replace(b"0x80010010", b"0x80010020") if dependency == symbols else saved+b"changed")
            with patch.object(runner.subprocess, "run") as process:
                try:
                    runner.run_audio(args, scene, build, checkpoints, root/"us.pack")
                except ValueError as error:
                    assert "stale audio reference identity" in str(error), error
                else:
                    raise AssertionError("stale reference identity accepted")
                process.assert_not_called()
            dependency.write_bytes(saved)
        (reference / "run-1-1.spu").write_text("tampered")
        with patch.object(runner.subprocess, "run") as process:
            try:
                runner.run_audio(args, scene, build, checkpoints, root / "us.pack")
            except ValueError as error:
                assert "missing/stale audio reference" in str(error)
            else:
                raise AssertionError("tampered audio evidence accepted")
            process.assert_not_called()
            assert not (build / "scenes/title_audio/audio.json").exists()
print("scene_runner: audio skip 77, two deterministic runs, no image capture, hash validation: OK")

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
            write_timing(kwargs["env"]["DW1_TIMING_TRACE"], "runLandingScreen")
            write_timing(runner.reference_path("title").with_suffix(".timing.jsonl"), "runLandingScreen")
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

# Typed state declarations and complete synthetic post-VBlank/rand traces.
with tempfile.TemporaryDirectory() as directory:
    root = Path(directory)
    symbols, declarations = root / "symbols.txt", root / "state_vars.txt"
    symbols.write_text("counter = 0x80101000;\npointer = 0x80101004;\nbios = 0x80000010;\n")
    declarations.write_text("# comment\ncounter s16[2] # array\npointer ptr\n")
    variables = runner.state_variables(declarations, symbols)
    assert [v["count"] for v in variables] == [2, 1]
    for line, message in (("counter float", "invalid"), ("counter u8[0]", "count"),
                          ("counter s8[-1]", "invalid"), ("unknown u32", "reference symbol: unknown"),
                          ("bios u32", "game RAM"), ("counter u8\ncounter u8", "duplicate")):
        declarations.write_text(line)
        try:
            runner.state_variables(declarations, symbols)
        except ValueError as error:
            assert message in str(error), error
        else:
            raise AssertionError(line)
    declarations.write_text("counter u32")
    with patch.object(runner.subprocess, "run", return_value=subprocess.CompletedProcess([], 0, "", "")):
        try:
            runner.state_variables(declarations, symbols, root / "dw1")
        except ValueError as error:
            assert "native symbol: counter" in str(error)
        else:
            raise AssertionError("missing native symbol accepted")
    left, right = root / "port.state", root / "reference.state"
    rows = "1 state counter[0] -32768\n1 state counter[1] 32767\n1 state pointer[0] 0\n"
    rows += "".join(f"1 rand {i} {i * 3}\n" for i in range(16))
    rows += "2 state counter[0] 0\n2 state counter[1] 1\n2 state pointer[0] counter+2\n"
    left.write_text(rows); right.write_text(rows)
    assert runner.compare_state(left, right, variables, 2, True) == 0
    for changed, message in ((rows.replace("counter[1] 32767", "counter[1] 32766"), "VBlank 1: counter[1] port=32767 reference=32766"),
                             (rows.replace("rand 4 12", "rand 4 13"), "rand call 4 (port VBlank 1, reference VBlank 1) port=12 reference=13"),
                             (rows.replace("2 state counter[1] 1\n", ""), "incomplete"),
                             ("", "incomplete")):
        right.write_text(changed)
        try:
            runner.compare_state(left, right, variables, 2, True)
        except ValueError as error:
            assert message in str(error), error
        else:
            raise AssertionError("invalid/divergent trace accepted")
    no_rand = "\n".join(row for row in rows.splitlines() if " rand " not in row) + "\n"
    left.write_text(no_rand); right.write_text(no_rand)
    try:
        runner.compare_state(left, right, variables, 2, True)
    except ValueError as error:
        assert "got 0" in str(error)
    else:
        raise AssertionError("expect_rand accepted zero calls")
print("scene_runner: typed declarations, missing symbols, first variable/rand divergence, incomplete traces, expect_rand: OK")

# Logical checkpoint samples remain comparable across different CD timings.
with tempfile.TemporaryDirectory() as directory:
    root = Path(directory)
    left, right = root / "port.state", root / "reference.state"
    variables = [dict(name="counter", type="s16", count=1)]
    checkpoints = ["first", "loop#2"]
    def trace(origins):
        rows = []
        for cp, origin in zip(checkpoints, origins):
            for k in range(3):
                rows.append(f"{cp} {k} {origin+k} state counter[0] {k}\n")
            if cp == "first":
                rows.extend(f"{origin+2} rand {i} {i*3}\n" for i in range(16))
        return "".join(rows)
    left.write_text(trace([10, 20])); right.write_text(trace([30, 60]))
    assert runner.compare_state(left, right, variables, 100, True, checkpoints, 2) == 0
    original = right.read_text()
    for changed, message in (
        (original.replace("loop#2 1 61 state counter[0] 1", "loop#2 1 61 state counter[0] 9"),
         "checkpoint loop#2+1: counter[0] (port VBlank 21, reference VBlank 61)"),
        (original.replace("rand 4 12", "rand 4 13"), "rand call 4 (port VBlank 12, reference VBlank 32)"),
        (original.replace("loop#2 2 62 state counter[0] 2\n", ""), "incomplete"),
        (original.replace("loop#2 1 61", "loop#2 1 62"), "inconsistent checkpoint origin"),
        (original.replace("first 0 30", "unknown 0 30"), "invalid checkpoint"),
        (original.replace("counter[0] 2", "counter[0] 32768"), "invalid s16"),
        (original.replace("rand 4 12", "rand 5 12"), "invalid rand")):
        right.write_text(changed)
        try:
            runner.compare_state(left, right, variables, 100, True, checkpoints, 2)
        except ValueError as error:
            assert message in str(error), error
        else:
            raise AssertionError(message)
    base = dict(capture_vblank=100, checkpoints=checkpoints, window=2,
                inputs=[dict(after="first", vblank=0, buttons=[3]),
                        dict(after="loop#2", vblank=0, buttons=[])])
    runner.validate_scene(base)
    runner.validate_scene(base | dict(checkpoint_after={"loop#2": "first"}))
    for mapping in ({"first": "loop#2"}, {"loop#2": "missing"}, {"first": "first"}):
        try: runner.validate_scene(base | dict(checkpoint_after=mapping))
        except ValueError: pass
        else: raise AssertionError(mapping)
    for change in (dict(checkpoints=[]), dict(checkpoints=["first", "first"]),
                   dict(checkpoints=["first#0"]), dict(checkpoints=["first#-1"]),
                   dict(window=-1), dict(window=True),
                   dict(inputs=[dict(after="missing", vblank=0, buttons=[])]),
                   dict(inputs=list(reversed(base["inputs"]))),
                   dict(inputs=[dict(vblank=0, buttons=[])])):
        try:
            runner.validate_scene(base | change)
        except ValueError:
            pass
        else:
            raise AssertionError(change)
    symbols = root / "symbols"
    symbols.write_text("first = 0x80010000;\nloop = 0x80010004;\n")
    assert runner.checkpoint_symbols(base, symbols)[1]["occurrence"] == 2
    assert runner.checkpoint_symbols(base | dict(checkpoint_after={"loop#2": "first"}), symbols)[1]["after"] == "first"
    for scene, native, message in ((base | dict(checkpoints=["missing"]), None, "reference"),
                                   (base, root / "dw1", "native")):
        with patch.object(runner.subprocess, "run", return_value=subprocess.CompletedProcess([], 0, "", "")):
            try:
                runner.checkpoint_symbols(scene, symbols, native)
            except ValueError as error:
                assert message in str(error), error
            else:
                raise AssertionError("missing checkpoint symbol accepted")
print("scene_runner: checkpoint windows, independent clocks, ordered rand values, divergence diagnostics and checkpoint/input preflight: OK")

# Native preflight must distinguish exported functions from instrumented functions.
with tempfile.TemporaryDirectory() as directory:
    symbols = Path(directory) / "symbols"
    symbols.write_text('first = 0x80010000;\nrand = 0x80010004;\nVSync = 0x80010008;\n')
    def inspect(command, **kwargs):
        output = ('08010000 T first\n08010004 T rand\n08010008 T VSync\n' if command[0] == 'nm'
                  else ('call 123 <__cyg_profile_func_enter>\n' if '--disassemble=first' in command else 'ret\n'))
        return subprocess.CompletedProcess(command, 0, output, '')
    with patch.object(runner.subprocess, 'run', side_effect=inspect):
        assert runner.checkpoint_symbols(dict(checkpoints=['first']), symbols, Path(directory)/'dw1')
        for name in ('rand', 'VSync'):
            try:
                runner.checkpoint_symbols(dict(checkpoints=[name]), symbols, Path(directory)/'dw1')
            except ValueError as error:
                assert f'not instrumented: {name}' in str(error), error
            else:
                raise AssertionError('uninstrumented checkpoint accepted')
print('scene_runner: uninstrumented checkpoint preflight: OK')

# Exercise real main --state cache decisions without a disc or an emulator.
with tempfile.TemporaryDirectory() as directory:
    root = Path(directory)
    for name in ('tools/port/scenes/title.toml', 'tools/port/scenes/state_vars.txt',
                 'tools/port/scenes/run.py', 'config/us/symbols.txt',
                 'tools/port/reference/capture.py', 'tools/port/reference/capture.lua', 'tools/port/reference/hardware.lua'):
        path = root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text('# fixture\n')
    scene = dict(mark='target', capture_vblank=1, inputs=[])
    (root / 'tools/port/scenes/title.toml').write_text('mark="target"\ncapture_vblank=1\ninputs=[]\n')
    build = root / 'build/port/native-headless'
    build.mkdir(parents=True)
    (build / 'us.pack').touch()
    reference = root / 'build/reference/title/run-1.state'
    reference.parent.mkdir(parents=True)
    stamp = reference.parent / 'state.sha256'
    variables = [dict(name='counter', type='u8', count=1)]
    rows = '1 state counter[0] 42\n'
    for mode in ('valid', 'missing', 'stale'):
        captures, hosts = [], []
        def execute(command, **kwargs):
            if str(root / 'tools/port/reference/capture.py') in command:
                captures.append(command)
                assert '--state' in command
                reference.write_text(rows)
                stamp.write_text(runner.state_fingerprint(scene))
                return subprocess.CompletedProcess(command, 0)
            hosts.append(command)
            def flag(name): return Path(command[command.index(name)+1])
            write_timing(kwargs['env']['DW1_TIMING_TRACE'], 'target')
            write_timing(reference.with_suffix('.timing.jsonl'), 'target')
            Path(kwargs['env']['DW1_TRACE']).write_text('1 mark target\n')
            flag('--clock-trace').write_text('1 -1\n')
            flag('--capture').write_bytes(b'P6\n1 1\n255\n\0\0\0')
            flag('--state-trace').write_text(rows)
            return subprocess.CompletedProcess(command, 0, b'', b'')
        with patch.object(runner, 'ROOT', root), patch.dict(os.environ, {}, clear=True), \
             patch.object(sys, 'argv', ['run.py', 'title', '--build', 'native-headless', '--state']), \
             patch.object(runner, 'state_variables', return_value=variables), \
             patch.object(runner.subprocess, 'run', side_effect=execute), redirect_stdout(io.StringIO()):
            reference.write_text(rows)
            stamp.write_text(runner.state_fingerprint(scene) if mode != 'stale' else 'obsolete')
            if mode == 'missing': reference.unlink()
            assert runner.main() == 0
            assert len(captures) == (0 if mode == 'valid' else 1), mode
            assert len(hosts) == 2, mode
print('scene_runner: main --state valid/missing/stale cached references without disc: OK')

# XA has one function-return window, includes global SPU registers, and never
# accepts a matching prefix when the movie tails have different lengths.
with tempfile.TemporaryDirectory() as directory:
    root = Path(directory)
    actual, expected = root/"actual.spu", root/"expected.spu"
    scene = dict(xa=True, checkpoints=["MOV_playMovie", "runLandingScreen"],
                 audio=[dict(start="MOV_playMovie", finish="MOV_playMovie")])
    assert len(runner.audio.validate_segments(scene)) == 1
    for segments in ([], [dict(start="MOV_playMovie", finish="MOV_playMovie"),
                          dict(start="runLandingScreen", vblanks=1)]):
        try: runner.audio.validate_segments(dict(scene, audio=segments))
        except ValueError as error: assert "XA audio requires exactly one segment" in str(error)
        else: raise AssertionError("XA scenes must declare exactly one audio segment")
    actual.write_text("# dw1-spu-trace-v1\n0 1 1b0 3fff\n# end 100 return\n")
    expected.write_text("# dw1-spu-trace-v1\n4 20 1b0 3fff\n# end 200 return\n")
    assert runner.audio.compare_voice_content(actual,expected,all_registers=True)["writes"] == 1
    for text in ("# dw1-spu-trace-v1\n0 1 1b0 3ffe\n# end 200 return\n",
                 "# dw1-spu-trace-v1\n0 1 1b0 3fff\n0 150 1aa c001\n# end 200 return\n"):
        expected.write_text(text)
        try: runner.audio.compare_voice_content(actual,expected,all_registers=True)
        except ValueError: pass
        else: raise AssertionError("XA must compare all ordered registers through both ends")
    for path in (actual,expected): path.write_text("# dw1-spu-trace-v1\n# end 100 return\n")
    assert runner.audio.compare_voice_content(actual,expected,all_registers=True)["writes"] == 0
print("scene_runner: XA single return window, all SPU registers, full tails and empty writes OK")

with tempfile.TemporaryDirectory() as directory:
    import struct
    root=Path(directory)
    raw, wav = root/"tap.xa", root/"tap.wav"
    raw.write_bytes(b"DW1XA1\n"+b"".join(struct.pack("<Qhh",cycle,value,-value)
                    for cycle,value in ((10,1),(10,2),(20,3),(30,4))))
    identity_module.xa_to_wav(raw,wav,10,30)
    with wave.open(str(wav),"rb") as stream:
        assert stream.getnframes()==3
        assert stream.readframes(3)==struct.pack("<hhhhhh",1,-1,2,-2,3,-3)
    for data in (b"bad", b"DW1XA1\nx", b"DW1XA1\n"+struct.pack("<QhhQhh",20,1,2,10,1,2)):
        raw.write_bytes(data)
        try: identity_module.xa_to_wav(raw,wav,0,30)
        except ValueError: pass
        else: raise AssertionError("malformed XA tap must fail")
    actual,expected=root/"a.wav",root/"b.wav"
    for path in (actual,expected):
        with wave.open(str(path),"wb") as stream:
            stream.setparams((2,2,44100,0,"NONE","not compressed"))
            stream.writeframes(struct.pack("<hh",1000,-1000)*264600)
    with patch.object(runner.audio,"alignment",return_value=0):
        result=runner.audio.compare_pcm(actual,expected,whole_window=True)
    assert len(result["windows"])==1 and result["compared_frames"]==264600
print("scene_runner: timestamped XA tap validation and full-window SNR OK")

# Movie gates apply to every RGB channel and use delivery cycles, not wall time.
movie_spec = importlib.util.spec_from_file_location('movie', Path(__file__).with_name('movie.py'))
movie = importlib.util.module_from_spec(movie_spec)
movie_spec.loader.exec_module(movie)
with tempfile.TemporaryDirectory() as tmp:
    root=Path(tmp); actual=root/'port'; reference=root/'ref'
    for directory in (actual,reference):
        directory.mkdir()
        rows=[dict(event='start',cycle=0),dict(event='frame',index=1,cycle=1000,vblank=2),
              dict(event='frame',index=2,cycle=2000,vblank=4),
              dict(event='end',cycle=2200,vblank=4,**{'return':1,'xa_cycle':1700})]
        (directory/'movie.jsonl').write_text('\n'.join(map(json.dumps,rows))+'\n')
        for i in (1,2): (directory/f'frame-{i:04d}.ppm').write_bytes(b'P6\n320 240\n255\n'+bytes(230400))
    assert not movie.evidence(root/'absent')
    scene=dict(expected_frames=2,expected_return=1)
    assert not movie.compare(actual,reference,scene,root/'diff')['failures']
    movie.deterministic(actual,reference)
    frame=actual/'frame-0002.ppm'
    frame.write_bytes(b'P6\n320 240\n255\n'+b'\x04'+bytes(230399))
    assert not movie.compare(actual,reference,scene,root/'diff')['failures']
    frame.write_bytes(b'P6\n320 240\n255\n'+b'\x05'+bytes(230399))
    result=movie.compare(actual,reference,scene,root/'diff')
    assert result['failures'] and result['worst_frame']==2
    assert (root/'diff/worst-frame-0002.ppm').is_file()
    frame.write_bytes(b'P6\n320 240\n255\n'+b'\x01\x00\x00'*76800)
    assert movie.compare(actual,reference,scene,root/'diff')['frames'][1]['channel_means']==[1,0,0]
    assert movie.compare(actual,reference,scene,root/'diff')['failures']
    frame.write_bytes((reference/frame.name).read_bytes())
    rows[-1]['xa_cycle']=1
    (actual/'movie.jsonl').write_text('\n'.join(map(json.dumps,rows))+'\n')
    assert any('A/V' in f for f in movie.compare(actual,reference,scene,root/'diff')['failures'])
    rows[-1].update(xa_cycle=1700,vblank=5)
    rows[-1]['return']=0
    (actual/'movie.jsonl').write_text('\n'.join(map(json.dumps,rows))+'\n')
    assert any('skip VBlank' in f for f in movie.compare(actual,reference,dict(expected_return=0),root/'diff')['failures'])
    try: movie.deterministic(actual,reference)
    except ValueError: pass
    else: raise AssertionError('non-deterministic movie accepted')
    frame.unlink()
    assert movie.evidence(actual) == {}
    try: movie.sequence(actual)
    except ValueError: pass
    else: raise AssertionError('missing movie frame accepted')
    # Start gates return timing; decoding more frames before Start is diagnostic.
    rows=[json.loads(line) for line in (reference/'movie.jsonl').read_text().splitlines()]
    rows[-1]['return']=0
    (reference/'movie.jsonl').write_text('\n'.join(map(json.dumps,rows))+'\n')
    (actual/'movie.jsonl').write_text('\n'.join(map(json.dumps,[rows[0],rows[1],rows[-1]]))+'\n')
    assert not movie.compare(actual,reference,dict(expected_return=0),root/'diff')['failures']
    # No timing offset is accepted, including the previously permitted +1.
    skip = dict(expected_return=0)
    for late in (-1, 1, 2):
        (actual/'movie.jsonl').write_text('\n'.join(map(json.dumps,[rows[0],rows[1],dict(rows[-1],vblank=rows[-1]['vblank']+late)]))+'\n')
        assert any('skip VBlank' in f for f in movie.compare(actual,reference,skip,root/'diff')['failures'])
print('movie: per-channel tolerance, worst diff, complete sequence, determinism, return, skip VBlank and A/V bounds OK')

# Scene-owned allowances preserve all three failure paths from owner decision 5.
import tomllib
listed = tomllib.loads(Path(__file__).with_name('movie0.toml').read_text())['mdec_allowances']
with tempfile.TemporaryDirectory() as tmp:
    root=Path(tmp); actual=root/'port'; reference=root/'ref'
    header=b'P6\n320 240\n255\n'
    rows=[dict(event='start',cycle=0),dict(event='frame',index=1,cycle=1000,vblank=2),
          dict(event='frame',index=2,cycle=2000,vblank=4),
          dict(event='end',cycle=2200,vblank=4,**{'return':1,'xa_cycle':1700})]
    for directory in (actual,reference):
        directory.mkdir()
        (directory/'movie.jsonl').write_text('\n'.join(map(json.dumps,rows))+'\n')
        (directory/'frame-0001.ppm').write_bytes(header+bytes(230400))
    for item in listed:
        item=dict(item,frame=2)
        scene=dict(expected_frames=2,expected_return=1,mdec_allowances=[item])
        offset=(item['y']*320+item['x'])*3+'RGB'.index(item['channel'])
        left=bytearray(230400);right=bytearray(230400)
        left[offset]=item['port'];right[offset]=item['reference']
        (reference/'frame-0002.ppm').write_bytes(header+right)
        def check(data):
            (actual/'frame-0002.ppm').write_bytes(header+data)
            return movie.compare(actual,reference,scene,root/'diff')
        result=check(left)
        assert not result['failures'] and result['accepted_mdec_allowances']==[dict(item,error=5)]
        # Failure 1: a different component exceeds four, even in the same pixel.
        other=left.copy();other[offset-1]=5
        assert any('unexpected=' in f for f in check(other)['failures'])
        # Failure 2: a listed component worsens beyond its listed error.
        worse=left.copy();worse[offset]+=1
        assert any('unexpected=' in f for f in check(worse)['failures'])
        # Failure 3: an accepted pair cannot exempt the per-channel mean.
        mean=left.copy();mean[0::3]=bytes([1])*76800
        result=check(mean)
        assert result['accepted_mdec_allowances'] and result['frames'][1]['channel_means'][0]==1
        assert result['failures'] and result['worst_frame']==2
        # The allowance is an exact pair, not a blanket five-unit tolerance.
        shifted=left.copy();shifted[offset]+=1
        changed=right.copy();changed[offset]+=1
        (reference/'frame-0002.ppm').write_bytes(header+changed)
        assert check(shifted)['failures']
        (reference/'frame-0002.ppm').write_bytes(header+right)
        improved=left.copy();improved[offset]-=1
        assert not check(improved)['failures']
print('movie allowances: exact listed pairs pass; unlisted, worsened and mean violations fail')

# Card inputs and scene-specific state declarations are part of reference identity.
with tempfile.TemporaryDirectory() as directory:
    root = Path(directory) / "repo"
    card_root = Path(directory) / "cards"
    (root / "tools/port/scenes").mkdir(parents=True)
    for name in ("tools/port/scenes/run.py", "tools/port/reference/capture.py", "tools/port/reference/capture.lua", "tools/port/reference/hardware.lua", "config/us/symbols.txt"):
        path = root / name; path.parent.mkdir(parents=True, exist_ok=True); path.write_text("fixture")
    (root / "tools/port/scenes/card_vars.txt").write_text("MONEY u32\n")
    with patch.object(runner, "ROOT", root), patch.dict(os.environ, {"DW1_CARD_DIR": str(card_root)}):
        scene = dict(id="continue", card1="save", state_vars="card_vars.txt")
        source = runner.final_card("port", "save")
        source.parent.mkdir(parents=True); source.write_bytes(bytes(131072))
        before = runner.state_fingerprint(scene, "bios")
        (root / "tools/port/scenes/card_vars.txt").write_text("MONEY u16\n")
        assert runner.state_fingerprint(scene, "bios") != before
        before = runner.state_fingerprint(scene, "bios")
        emulator = root / "pcsx-redux"
        emulator.write_bytes(b"first binary")
        with patch.object(runner.shutil, "which", return_value=str(emulator)):
            binary_before = runner.state_fingerprint(scene, "bios")
            emulator.write_bytes(b"patched binary")
            assert runner.state_fingerprint(scene, "bios") != binary_before
        source.write_bytes(bytes([1]) * 131072)
        assert runner.state_fingerprint(scene, "bios") != before
        paths = runner.prepare_cards(scene, "reference", 1)
        assert paths[0].read_bytes() == source.read_bytes() and not paths[1].exists()
        runner.export_card(scene, "reference", 1, paths)
        paths = runner.prepare_cards(scene, "reference", 2)
        runner.export_card(scene, "reference", 2, paths)
        paths[0].write_bytes(bytes(131072))
        try: runner.export_card(scene, "reference", 2, paths)
        except ValueError: pass
        else: raise AssertionError("different final card must fail")
        with patch.dict(os.environ, {"DW1_CARD_DIR": str(root / "cards")}):
            try: runner.card_root()
            except ValueError: pass
            else: raise AssertionError("cards must stay outside checkout")
# A matching state stamp cannot replace a missing/corrupt export for this preset.
with tempfile.TemporaryDirectory() as directory:
    root = Path(directory) / "repo"
    scenes = root / "tools/port/scenes"
    scenes.mkdir(parents=True)
    (scenes / "card_reuse.toml").write_text('id = "card_reuse"\ncard1 = "new"\ncapture_vblank = 1\n')
    reference = root / "build/reference/card_reuse/run-1.ppm"
    reference.parent.mkdir(parents=True)
    reference.with_suffix(".state").touch()
    (reference.parent / "state.sha256").write_text("current")
    missing_pack = root / "missing.pack"
    with patch.object(runner, "ROOT", root), patch.dict(os.environ, {"DW1_CARD_DIR": str(Path(directory) / "cards")}), \
         patch.object(sys, "argv", ["run.py", "card_reuse", "--build", "native-asan", "--state", "--pack", str(missing_pack)]), \
         patch.object(runner, "reference_path", return_value=reference), \
         patch.object(runner, "state_fingerprint", return_value="current"), \
         patch.object(runner, "checkpoint_symbols", return_value=[]), patch.object(runner, "state_variables", return_value=[]):
        for preset in ("native-headless", "native-asan"):
            card = runner.final_card("reference", "card_reuse", preset)
            card.parent.mkdir(parents=True, exist_ok=True)
            card.write_bytes(bytes(131072))
            card.with_suffix(".sha256").write_text(hashlib.sha256(card.read_bytes()).hexdigest())
        current = runner.final_card("reference", "card_reuse", "native-asan")
        for condition in ("valid", "missing", "size", "hash"):
            current.write_bytes(bytes(131072))
            if condition == "missing": current.unlink()
            if condition == "size": current.write_bytes(bytes(64))
            if condition == "hash": current.write_bytes(bytes([1]) * 131072)
            with patch.object(runner.subprocess, "run", return_value=SimpleNamespace(returncode=73)) as process:
                if condition == "valid":
                    try: runner.main()
                    except ValueError as error: assert str(missing_pack) in str(error)
                    else: raise AssertionError("valid cache should continue to Pack validation")
                    process.assert_not_called()
                else:
                    assert runner.main() == 73
                    command = process.call_args.args[0]
                    assert command[command.index("--card-build") + 1] == "native-asan"
print("scene_runner: state reuse requires a valid card export for the current preset OK")

for key, value in (("card1", "../escape"), ("card1", 5), ("state_vars", "../escape.txt"), ("state_vars", [])):
    try: runner.validate_scene(dict(capture_vblank=1, **{key:value}))
    except ValueError: pass
    else: raise AssertionError(f"invalid {key} accepted")
print("scene_runner: external new/source cards, deterministic export, card/state fingerprint and path validation OK")

with tempfile.TemporaryDirectory() as directory:
    root = Path(directory)
    symbols, declarations = root / "symbols", root / "vars"
    symbols.write_text("ENTITY = 0x80010000;\n")
    declarations.write_text("ENTITY+56 u8[2]\n")
    variables = runner.state_variables(declarations, symbols)
    assert variables[0]["address"] == 0x80010038
    saved, loaded = root / "saved", root / "loaded"
    rows = "loadSavegame 0 10 state ENTITY+56[0] 1\nloadSavegame 0 10 state ENTITY+56[1] 2\n"
    saved.write_text(rows); loaded.write_text(rows.replace(" 10 ", " 20 "))
    runner.compare_saved_state(saved, loaded, variables, ["loadSavegame"], 0)
    loaded.write_text(loaded.read_text().replace("[1] 2", "[1] 3"))
    try: runner.compare_saved_state(saved, loaded, variables, ["loadSavegame"], 0)
    except ValueError: pass
    else: raise AssertionError("a changed restored payload must fail")
print("scene_runner: entity offsets and saved/restored payload comparison OK")

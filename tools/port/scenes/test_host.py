#!/usr/bin/env python3
"""L1 host CLI, missing-mark failure, and capture timing without disc data."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

exe, probe, build = map(Path, sys.argv[1:4])
with tempfile.TemporaryDirectory(dir=build) as tmp:
    tmp = Path(tmp)
    env = dict(os.environ, DW1_TRACE=str(tmp / "host.trace"))
    def run(command, status, extra=None):
        result = subprocess.run(command, env=env | (extra or {}), cwd=tmp,
                                capture_output=True, timeout=60)
        assert result.returncode == status, (command, result.returncode, result.stdout, result.stderr)
        assert b"AddressSanitizer" not in result.stderr and b"runtime error" not in result.stderr, result.stderr
        return result
    for flags in (["--capture-mark", "target"], ["--capture", "x"],
                  ["--capture-vblank", "1"], ["--clock-trace"],
                  *[["--capture-mark", "target", "--capture", "x", "--capture-vblank", n]
                    for n in ("0", "-1", "4294967296", "1x", "", "999999999999999999999")]):
        run([str(exe), *flags], 2)
    pack = build / "pack_t1.pack" # CTest fixture: entirely synthetic disc bytes.
    capture, clock = tmp / "absent.ppm", tmp / "host.clock"
    result = run([str(exe), "--frames", "1", "--capture-mark", "absent",
         "--capture-vblank", "60", "--capture", str(capture), "--clock-trace", str(clock)], 1)
    assert b"missing native mark symbol: absent" in result.stderr
    assert not capture.exists()
    result = run([str(probe), str(capture), str(clock)], 1, {"TEST_PACK": str(pack)})
    assert b"reached frame 2" in result.stdout and not capture.exists()
    capture = tmp / "pixel.ppm"
    result = run([str(probe), str(capture), str(clock)], 0)
    assert b"at VBlank 2" in result.stdout, result.stdout
    assert capture.read_bytes() == b"P6\n1 1\n255\n\xff\x00\x00"
    capture.unlink()
    result = run([str(probe), str(capture), str(clock)], 0, {"TEST_MARK_LATE": "1"})
    assert b"at VBlank 7\n" in result.stdout, result.stdout
    assert capture.read_bytes() == b"P6\n1 1\n255\n\xff\x00\x00"
    capture.unlink()
    result = run([str(probe), str(capture), str(clock)], 1, {"TEST_UNTIL_OVERLAY": "1"})
    assert b"overlay loaded" in result.stdout and not capture.exists()
    result = run([str(probe), str(capture), str(clock)], 1, {"TEST_DISPLAY_OFF": "1"})
    assert b"display is disabled" in result.stderr and not capture.exists()
    missing_capture = tmp / "nonexistent" / "capture.ppm"
    result = run([str(probe), str(missing_capture), str(clock)], 1)
    assert b"port: capture:" in result.stderr and not missing_capture.exists()
    # Only the clock trace uses the device: capture failure cleanup removes the PPM path.
    result = run([str(probe), str(capture), "/dev/full"], 1)
    assert b"cannot write capture or clock trace" in result.stderr and not capture.exists()
    print("scene_flags: invalid flags, synthetic Pack missing mark, first mark, relative VBlank interval, pending overlay capture, post-event capture, P6 RGB, disabled display, capture/trace I/O failures: OK")

if len(sys.argv) > 4:
    with tempfile.TemporaryDirectory(dir=build) as directory:
        output = Path(directory)
        inputs, variables = output / "inputs.txt", output / "state_vars.txt"
        inputs.write_text("0 0008\n2 4000\n4 0000\n")
        variables.write_text("TEST_VALUES s16[2]\nTEST_POINTERS ptr[3]\n")
        for base in ("frame0", "mark"):
            trace = output / f"{base}.state"
            process = subprocess.run([sys.argv[4], str(output / f"{base}.ppm"), str(inputs),
                                      str(variables), str(trace), base], capture_output=True,
                                     env=os.environ, timeout=30)
            assert process.returncode == 0, (process.returncode, process.stdout, process.stderr)
            rows = trace.read_text().splitlines()
            assert len(rows) == 30, rows  # 5 VBlanks * 5 variables + 5 rand calls
            assert rows[0] == "0 rand 0 0", rows
            for k in range(1, 6):
                assert f"{k} state TEST_VALUES[0] -32768" in rows
                assert f"{k} state TEST_VALUES[1] 32767" in rows
                assert f"{k} state TEST_POINTERS[0] 0x80000123" in rows
                assert f"{k} state TEST_POINTERS[1] TEST_VALUES+2" in rows
                assert f"{k} state TEST_POINTERS[2] 0" in rows
        print("scene_state: both origins, held input after pad reset, signed arrays, arena/global/NULL pointers, rand timestamps: OK")

    with tempfile.TemporaryDirectory(dir=build) as directory:
        output = Path(directory)
        inputs, variables, checkpoints = (output / n for n in ("inputs", "variables", "checkpoints"))
        inputs.write_text("TEST_CHECKPOINT 0 0008\nTEST_CHECKPOINT 2 0000\nTEST_CHECKPOINT#2 0 4000\n")
        variables.write_text("TEST_VALUES s16[2]\nTEST_POINTERS ptr[3]\n")
        checkpoints.write_text("TEST_CHECKPOINT 1\nTEST_CHECKPOINT#2 1\n")
        trace = output / "checkpoints.state"
        process = subprocess.run([sys.argv[4], str(output / "capture.ppm"), str(inputs),
                                  str(variables), str(trace), "mark", str(checkpoints)],
                                 capture_output=True, env=os.environ, timeout=30)
        assert process.returncode == 0, (process.returncode, process.stdout, process.stderr)
        rows = trace.read_text().splitlines()
        for cp, relative, absolute in (("TEST_CHECKPOINT", 0, 0), ("TEST_CHECKPOINT", 1, 1),
                                       ("TEST_CHECKPOINT#2", 0, 3), ("TEST_CHECKPOINT#2", 1, 4)):
            assert f"{cp} {relative} {absolute} state TEST_VALUES[0] -32768" in rows, rows
        assert len([r for r in rows if " state " in r]) == 20, rows
        assert len([r for r in rows if " rand " in r]) == 4, rows
        print("scene_state: instrumented nth-entry checkpoints, entry/window sampling, per-checkpoint input latches and completion: OK")

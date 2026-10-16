#!/usr/bin/env python3
"""L1 host CLI, missing-mark failure, and capture timing without disc data."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

exe, probe, build = map(Path, sys.argv[1:])
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
    run([str(exe), "--frames", "1", "--capture-mark", "absent",
         "--capture-vblank", "60", "--capture", str(capture), "--clock-trace", str(clock)], 1)
    assert not capture.exists() and clock.stat().st_size
    assert "mark absent" not in (tmp / "host.trace").read_text()
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

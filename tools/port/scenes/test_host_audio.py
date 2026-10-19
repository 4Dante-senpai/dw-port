#!/usr/bin/env python3
"""Exercise the production scene capture runtime without game data or images."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import wave

with tempfile.TemporaryDirectory() as directory:
    output = Path(directory)
    checkpoints = output / "checkpoints"
    checkpoints.write_text("AUDIO_START 0\nAUDIO_FINISH 0\nAUDIO_MUSIC 0\n")
    segments = output / "segments"
    segments.write_text(f"AUDIO_START AUDIO_FINISH 0 {output}/1.spu {output}/1.wav\n"
                        f"AUDIO_MUSIC - 2 {output}/2.spu {output}/2.wav\n")
    environment = dict(os.environ)
    environment["ASAN_OPTIONS"] = environment.get("ASAN_OPTIONS", "") + ":detect_stack_use_after_return=0"
    result = subprocess.run([sys.argv[1], str(checkpoints), str(segments)], env=environment,
                            capture_output=True, text=True, timeout=30)
    assert result.returncode == 0, (result.stdout, result.stderr)
    assert "AddressSanitizer" not in result.stderr and "runtime error" not in result.stderr
    frames = [int(value) for value in next(line for line in result.stdout.splitlines()
                                         if line.startswith("frames ")).split()[1:]]
    for index, values in ((1, ["1234", "2345"]), (2, ["3456", "4567"])):
        rows = (output / f"{index}.spu").read_text().splitlines()
        assert rows[0] == "# dw1-spu-trace-v1"
        assert [row.split()[3] for row in rows[1:]] == values, rows
        assert [row.split()[0] for row in rows[1:]] == ["0", "1"], rows
        assert rows[1].split()[1] == "0", rows
        with wave.open(str(output / f"{index}.wav")) as wav:
            assert (wav.getnchannels(), wav.getsampwidth(), wav.getframerate()) == (2, 2, 44100)
            assert wav.getnframes() == frames[index - 1]
    assert not list(output.glob("*.ppm"))
    # Direct CLI use must enforce the same external-evidence policy as Python.
    repo = Path(__file__).resolve().parents[3]
    alias = output / "repo"
    alias.symlink_to(repo, target_is_directory=True)
    for path in (repo / "build/forbidden-audio.spu", alias / "build/forbidden-audio.spu"):
        segments.write_text(f"AUDIO_START AUDIO_FINISH 0 {path} {output}/1.wav\n"
                            f"AUDIO_MUSIC - 2 {output}/2.spu {output}/2.wav\n")
        result = subprocess.run([sys.argv[1], str(checkpoints), str(segments)], env=environment,
                                capture_output=True, text=True, timeout=30)
        assert result.returncode == 1 and "outside the repository" in result.stderr, result.stderr
        assert not path.exists()
print("scene_audio_runtime: disjoint checkpoints, independent origins, automatic stop, WAV lengths: OK")

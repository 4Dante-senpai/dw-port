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
    checkpoints.write_text("AUDIO_START 0\nAUDIO_FINISH 0\nAUDIO_MUSIC 0 AUDIO_FINISH\n")
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
        assert [row.split()[3] for row in rows[1:-1]] == values, rows
        assert [row.split()[0] for row in rows[1:-1]] == ["0", "1"], rows
        assert rows[1].split()[1] == "0", rows
        with wave.open(str(output / f"{index}.wav")) as wav:
            assert (wav.getnchannels(), wav.getsampwidth(), wav.getframerate()) == (2, 2, 44100)
            assert wav.getnframes() == frames[index - 1]
    assert not list(output.glob("*.ppm"))
    # A video/audio interval may end before its final strict checkpoint.
    # Keep running until the checkpoint is recorded, without extending audio.
    checkpoints.write_text(checkpoints.read_text() + "AUDIO_LANDING 0\n")
    late = subprocess.run([sys.argv[1], str(checkpoints), str(segments)], env=environment,
                          capture_output=True, text=True, timeout=30)
    assert late.returncode == 0, (late.stdout, late.stderr)
    assert (output / "2.spu").read_text().splitlines()[-1].endswith(" vblanks")
    checkpoints.write_text(checkpoints.read_text().replace("AUDIO_LANDING 0\n", ""))
    segments.write_text(f"AUDIO_START AUDIO_FINISH 0 {output}/1.spu {output}/1.wav\n"
                        f"AUDIO_MUSIC - 0 {output}/2.spu {output}/2.wav AUDIO_STOP\n")
    event = subprocess.run([sys.argv[1], str(checkpoints), str(segments)], env=environment,
                           capture_output=True, text=True, timeout=30)
    assert event.returncode == 0, (event.stdout, event.stderr)
    event_rows = (output/"2.spu").read_text().splitlines()
    assert event_rows[-1].endswith(" event:AUDIO_STOP"), event_rows
    assert [row.split()[3] for row in event_rows[1:-1]] == ["3456", "4567"]
    assert (output/"1.spu").read_text().splitlines()[-1].endswith(" checkpoint")
    # The production detector must retain the complete bundle, reject inherited
    # volume/unrelated KON, and rebase both exact trace cycles and PCM bounds.
    segments.write_text(segments.read_text().replace("AUDIO_STOP\n", "AUDIO_STOP note_on\n"))
    missing = subprocess.run([sys.argv[1], str(checkpoints), str(segments)], env=environment,
                             capture_output=True, text=True, timeout=30)
    assert missing.returncode == 1 and "missing new note-on" in missing.stderr
    note = subprocess.run([sys.argv[1], str(checkpoints), str(segments)],
                          env=dict(environment, DW1_TEST_NOTE_ANCHOR="1"),
                          capture_output=True, text=True, timeout=30)
    assert note.returncode == 0, (note.stdout, note.stderr)
    rows = (output/"2.spu").read_text().splitlines()[1:-1]
    assert rows[0] == "0 0 004 0400", rows
    assert [row.split()[3] for row in rows] == ["0400", "0123", "0234", "0345", "0456", "0567", "0001", "0000", "4567"]
    assert rows[1] == "0 1 000 0123", rows
    frames = int(next(line for line in note.stdout.splitlines() if line.startswith("frames ")).split()[2])
    with wave.open(str(output/"2.wav")) as wav:
        assert wav.getnframes() == frames
    high = subprocess.run([sys.argv[1], str(checkpoints), str(segments)],
                          env=dict(environment, DW1_TEST_NOTE_ANCHOR="high"),
                          capture_output=True, text=True, timeout=30)
    assert high.returncode == 0, (high.stdout, high.stderr)
    rows = (output/"2.spu").read_text().splitlines()[1:-1]
    assert rows[0] == "0 0 114 0400" and rows[-2].endswith("18a 0002"), rows
    # Direct CLI use must enforce the same source exclusion as Python.
    repo = Path(__file__).resolve().parents[3]
    alias = output / "repo"
    alias.symlink_to(repo, target_is_directory=True)
    for path in (repo / "src/forbidden-audio.spu", alias / "src/forbidden-audio.spu"):
        segments.write_text(f"AUDIO_START AUDIO_FINISH 0 {path} {output}/1.wav\n"
                            f"AUDIO_MUSIC - 2 {output}/2.spu {output}/2.wav\n")
        result = subprocess.run([sys.argv[1], str(checkpoints), str(segments)], env=environment,
                                capture_output=True, text=True, timeout=30)
        assert result.returncode == 1 and "outside repository sources" in result.stderr, result.stderr
        assert not path.exists()
    with tempfile.TemporaryDirectory(dir=repo / "build") as persistent:
        persistent = Path(persistent)
        for directory in (persistent, alias / persistent.relative_to(repo)):
            path = directory / "allowed.spu"
            segments.write_text(f"AUDIO_START AUDIO_FINISH 0 {path} {output}/1.wav\n"
                                f"AUDIO_MUSIC - 2 {output}/2.spu {output}/2.wav\n")
            result = subprocess.run([sys.argv[1], str(checkpoints), str(segments)], env=environment,
                                    capture_output=True, text=True, timeout=30)
            assert result.returncode == 0, (result.stdout, result.stderr)
            assert path.read_text().startswith("# dw1-spu-trace-v1\n")
        source_alias = persistent / "sources"
        source_alias.symlink_to(repo / "src", target_is_directory=True)
        path = source_alias / "forbidden-audio.spu"
        segments.write_text(f"AUDIO_START AUDIO_FINISH 0 {path} {output}/1.wav\n")
        result = subprocess.run([sys.argv[1], str(checkpoints), str(segments)], env=environment,
                                capture_output=True, text=True, timeout=30)
        assert result.returncode == 1 and "outside repository sources" in result.stderr, result.stderr
        assert not path.exists()
print("scene_audio_runtime: disjoint checkpoints, independent origins, automatic stop, WAV lengths: OK")

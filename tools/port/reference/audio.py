"""Pre-device Redux PCM capture. Evidence stays outside the repository."""
import hashlib
import importlib.util
import json
import os
import re
from pathlib import Path
import struct
import wave

_spec = importlib.util.spec_from_file_location("scene_audio", Path(__file__).parents[1] / "scenes/audio.py")
compare = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(compare)
_timing_spec = importlib.util.spec_from_file_location("scene_timing", Path(__file__).parents[1] / "scenes/timing.py")
timing = importlib.util.module_from_spec(_timing_spec)
_timing_spec.loader.exec_module(timing)
_movie_spec = importlib.util.spec_from_file_location("scene_movie", Path(__file__).parents[1] / "scenes/movie.py")
movie = importlib.util.module_from_spec(_movie_spec)
_movie_spec.loader.exec_module(movie)

def evidence(scene, output=None):
    names = {f"run-{run}-{segment}.{suffix}" for run in (1,2)
            for segment in range(1,len(scene["audio"])+1)
            for suffix in (("spu","wav","xa.wav") if scene.get("xa") else ("spu","wav"))}
    names.update(f"run-{i}.{kind}.jsonl" for i in (1, 2) for kind in ("timing", "hardware"))

    if scene.get("movie") and output:
        for run in (1,2): names.update(movie.evidence(output / f"movie-{run}"))
    return names


def xa_to_wav(raw, output, start_cycle, end_cycle):
    """Concatenate decoder output from all sectors delivered within the movie window."""
    with raw.open("rb") as source, wave.open(str(output),"wb") as target:
        target.setparams((2,2,44100,0,"NONE","not compressed"))
        if source.read(7)!=b"DW1XA1\n": raise ValueError("missing Redux XA tap header")
        previous=-1
        count=0
        while record:=source.read(12):
            if len(record)!=12: raise ValueError("truncated XA tap")
            cycle,l,r=struct.unpack("<Qhh",record)
            if cycle<previous: raise ValueError("backwards XA tap cycle")
            previous=cycle
            if start_cycle<=cycle<end_cycle:
                target.writeframesraw(struct.pack("<hh",l,r)); count+=1
        if not count: raise ValueError("empty XA window")


def tap_to_wav(raw, output, start, end):
    """Select [start, end) mixer samples without a sink, resampling or gain fitting.

    Every chunk carries its absolute mixer position. Reject missing, overlapping,
    backwards or truncated data in the selected interval instead of filling zeros.
    Chunks before the interval may contain boot clock rebases and are ignored.
    """
    if type(start) is not int or type(end) is not int or not 0 <= start < end:
        raise ValueError("invalid PCM segment bounds")
    with raw.open("rb") as source, wave.open(str(output), "wb") as target:
        target.setparams((2, 2, 44100, 0, "NONE", "not compressed"))
        if source.read(8) != b"DW1PCM1\n":
            raise ValueError("missing Redux PCM tap header (patched emulator required)")
        cursor = start
        while header := source.read(12):
            if len(header) != 12:
                raise ValueError("truncated PCM tap chunk header")
            first, frames = struct.unpack("<QI", header)
            if not 0 < frames <= 8192:
                raise ValueError("invalid PCM tap chunk size")
            data = source.read(frames * 4)
            if len(data) != frames * 4:
                raise ValueError("truncated PCM tap chunk")
            stop = first + frames
            if stop <= start or first >= end:
                continue
            lo, hi = max(first, start), min(stop, end)
            if lo != cursor:
                raise ValueError(f"discontinuous PCM tap: expected sample {cursor}, got {lo}")
            target.writeframesraw(data[(lo-first)*4:(hi-first)*4])
            cursor = hi
        if cursor != end:
            raise ValueError(f"truncated PCM interval: expected through {end}, got {cursor}")


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def fingerprint(scene, bios, emulator, root):
    files = [root / "tools/port/reference" / name for name in ("capture.py", "capture.lua", "hardware.lua", "audio.py")]
    files += [root / "tools/port/scenes/audio.py",
              root / "tools/port/reference/pcsx-redux-pcm-tap.patch", bios, Path(emulator)]
    if scene.get("movie"): files.append(root / "tools/port/scenes/movie.py")
    source = (root / "config/us/symbols.txt").read_text()
    overlay=root / f"config/us/symbols_{scene.get('overlay', '')}.txt"
    if scene.get("overlay") and overlay.is_file(): source += "\n" + overlay.read_text()
    symbols = {name: int(address, 16) for name, address in re.findall(
        r"^([\w.$]+)\s*=\s*(0x[0-9a-fA-F]+);", source, re.M)}
    names = {"VSync", scene["mark"]}
    if scene.get("movie"): names.add("PutDispEnv")
    names.update(label.split("#")[0] for label in scene.get("checkpoints", []))
    for segment in scene["audio"]:
        names.update(segment.get("stop_on", []))
    if names - symbols.keys():
        raise ValueError(f"missing audio reference symbols: {sorted(names - symbols.keys())}")
    payload = dict(scene=scene, symbols={name: symbols[name] for name in sorted(names)}, files=[digest(path) for path in files], settings=dict(
        Backend="dummy", UseNullSync=True, Mute=False, Speed=1, Interp=2, KeyOnDelay=6,
        pcm_tap="DW1PCM1", audio_drain_vblanks=4))
    return hashlib.sha256(json.dumps(payload, sort_keys=True).encode()).hexdigest()


def capture_audio(args, scene, symbols, emulator, cue, bios, adapter):
    segments = compare.validate_segments(scene)
    output = compare.external_output(args.out or Path(os.environ.get(
        "DW1_REFERENCE_DIR", "/tmp/dw1-audio-reference")) / args.id, adapter.ROOT)
    output.mkdir(parents=True, exist_ok=True)
    stamp = adapter.ROOT / "build/reference" / args.id / "audio.json"
    identity = fingerprint(scene, bios, emulator, adapter.ROOT)
    if args.reuse and stamp.is_file():
        record = json.loads(stamp.read_text())
        if (record.get("fingerprint") == identity and record.get("output") == str(output)
                and set(record.get("hashes", {})) == evidence(scene, output)
                and all((output / name).is_file() and digest(output / name) == value
                        for name, value in record.get("hashes", {}).items())
                ):
            print(f"reference_{args.id}: cached audio (verified hashes)")
            return 0
    stamp.unlink(missing_ok=True)
    for index in (1, 2):
        for i in range(1,len(segments)+1):
            for suffix in ("spu", "wav", "json", "xa.wav"):
                (output / f"run-{index}-{i}.{suffix}").unlink(missing_ok=True)
        (output / f"run-{index}.timing.jsonl").unlink(missing_ok=True)
        (output / f"run-{index}.pcm").unlink(missing_ok=True)
        (output / f"run-{index}.xa").unlink(missing_ok=True)
    # The capped capture container must run only one emulator at a time.
    for index in (1, 2):
        adapter.run_once(emulator, cue, bios, scene, symbols,
                         output, index, args.timeout, args.max_vblanks)
    hashes = {}
    for index in (1, 2):
        timing_path = output / f"run-{index}.timing.jsonl"
        timing.read(timing_path)
        hashes[timing_path.name] = digest(timing_path)
        hardware_path = output / f"run-{index}.hardware.jsonl"
        adapter.hardware.summarize(hardware_path)
        hashes[hardware_path.name] = digest(hardware_path)
        log = (output / f"run-{index}.log").read_text(errors="replace")
        if "DW1 PCM tap: PCM16LE stereo 44100 Hz, indexed mixer samples" not in log:
            raise ValueError("Redux PCM tap unavailable; rebuild the patched emulator")
        for i, _ in enumerate(segments, 1):
            meta = json.loads((output / f"run-{index}-{i}.json").read_text())
            tap_to_wav(output / f"run-{index}.pcm", output / f"run-{index}-{i}.wav",
                        int(meta["start_cycle"]) // 768 + 1, int(meta["end_cycle"]) // 768 + 1)
            if scene.get("xa"):
                xa_to_wav(output / f"run-{index}.xa", output / f"run-{index}-{i}.xa.wav",
                          int(meta["start_cycle"]),int(meta["end_cycle"]))
            for suffix in (("spu","wav","xa.wav") if scene.get("xa") else ("spu","wav")):
                name = f"run-{index}-{i}.{suffix}"
                hashes[name] = digest(output / name)
    if hashes['run-1.hardware.jsonl'] != hashes['run-2.hardware.jsonl']:
        raise ValueError('non-deterministic reference hardware commands')
    if hashes['run-1.timing.jsonl'] != hashes['run-2.timing.jsonl']:
        raise ValueError('non-deterministic reference checkpoint timing')
    for i in range(1,len(segments)+1):
        for suffix in (("spu","wav","xa.wav") if scene.get("xa") else ("spu","wav")):
            if hashes[f"run-1-{i}.{suffix}"] != hashes[f"run-2-{i}.{suffix}"]:
                raise ValueError(f"non-deterministic reference segment {i} {suffix}; "
                                 "pre-device mixer capture differs")
        if not scene.get("xa") and not compare.read_trace(output / f"run-1-{i}.spu"):
            raise ValueError(f"empty reference SPU trace in segment {i}")
        compare.read_pcm(output / f"run-1-{i}.wav")
    if scene.get("movie"):
        for index in (1,2):
            movie.finish_reference(output / f"movie-{index}",output / f"run-{index}.xa")
            hashes.update(movie.evidence(output / f"movie-{index}"))
        movie.deterministic(output / "movie-1",output / "movie-2")
    stamp.parent.mkdir(parents=True, exist_ok=True)
    stamp.write_text(json.dumps(dict(fingerprint=identity, bios=str(bios.resolve()), scene=scene, output=str(output), hashes=hashes), indent=2) + "\n")
    print(f"reference_{args.id}: two byte-identical traces and PCM segments; {stamp}")
    return 0

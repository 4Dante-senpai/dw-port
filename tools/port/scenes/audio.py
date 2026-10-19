"""Decision 4 voice-content gate, rule B diagnostics and stereo PCM metrics."""
from array import array
from collections import defaultdict
import cmath
import math
from pathlib import Path
import sys
import wave

RATE = 44100
WINDOW = RATE * 5


def external_output(path, root):
    """Audio evidence must never be written anywhere inside the checkout."""
    path = Path(path).resolve()
    if path.is_relative_to(Path(root).resolve()):
        raise ValueError("audio output must be outside the repository")
    return path


def trace_data(path):
    rows, end = [], None
    previous = (-1, -1)
    with Path(path).open() as stream:
        if stream.readline().strip() != "# dw1-spu-trace-v1":
            raise ValueError(f"invalid SPU trace header: {path}")
        for line_number, line in enumerate(stream, 2):
            fields = line.split()
            try:
                if end is not None:
                    raise ValueError()
                if len(fields) == 4 and fields[:2] == ["#", "end"]:
                    sample = int(fields[2])
                    if sample < max(0, previous[1]):
                        raise ValueError()
                    end = dict(sample=sample, reason=fields[3])
                    continue
                if len(fields) != 4:
                    raise ValueError()
                vb, sample = map(int, fields[:2])
                reg, value = (int(field, 16) for field in fields[2:])
                if (vb < 0 or sample < 0 or vb < previous[0] or sample < previous[1]
                        or not 0 <= reg < 0x200 or reg % 2 or not 0 <= value <= 65535):
                    raise ValueError()
            except ValueError:
                raise ValueError(f"invalid SPU trace row: {path}:{line_number}") from None
            rows.append((vb, sample, reg, value))
            previous = vb, sample
    return rows, end


def read_trace(path):
    groups = defaultdict(list)
    for vb, sample, reg, value in trace_data(path)[0]:
        groups[vb, reg].append((value, sample))
    return groups


def compare_voice_content(actual, reference, music=False, stop_on=()):
    """Whole segment 1; fixed common sample interval for note-anchored music."""
    a_rows, a_end = trace_data(actual)
    b_rows, b_end = trace_data(reference)
    common = None
    if music:
        for name, end in (("port", a_end), ("reference", b_end)):
            if not end or end["reason"] not in {f"event:{symbol}" for symbol in stop_on}:
                raise ValueError(f"{name} music lacks first game-event boundary; recapture at SEQ anchor")
            if end["sample"] < RATE * 9:
                raise ValueError(f"{name} music before first game event is {end['sample']/RATE:.6f} s; "
                                 "requires >= 9 s")
        common = min(a_end["sample"], b_end["sample"])
    left = [row for row in a_rows if row[2] < 0x180 and (common is None or row[1] < common)]
    right = [row for row in b_rows if row[2] < 0x180 and (common is None or row[1] < common)]
    offsets = []
    for index in range(max(len(left), len(right))):
        a = left[index] if index < len(left) else None
        b = right[index] if index < len(right) else None
        if a is None or b is None or a[2:] != b[2:]:
            def show(row):
                return ("missing" if row is None else
                        f"VBlank {row[0]} sample {row[1]} reg 0x{row[2]:03x}=0x{row[3]:04x}")
            raise ValueError(f"first voice-content divergence at write {index}: "
                             f"port={show(a)} reference={show(b)}")
        offsets.append(abs(a[1] - b[1]))
    if not left:
        raise ValueError("empty voice-content segment")
    return dict(writes=len(left), sample_offset_max=max(offsets),
                sample_offset_mean=sum(offsets)/len(offsets),
                common_window_samples=common,
                excluded_tail_writes=[sum(row[2] < 0x180 and row[1] >= common for row in rows)
                                      if common is not None else 0 for rows in (a_rows, b_rows)],
                port_end=a_end, reference_end=b_end)


def diagnostic(call, *args, **kwargs):
    try:
        return dict(status="match", metrics=call(*args, **kwargs))
    except ValueError as error:
        return dict(status="different", error=str(error))


def compare_trace(actual, reference):
    left, right = read_trace(actual), read_trace(reference)
    total = maximum = count = 0
    for vb, reg in sorted(left.keys() | right.keys()):
        a, b = left.get((vb, reg), []), right.get((vb, reg), [])
        for index in range(max(len(a), len(b))):
            av = a[index][0] if index < len(a) else None
            bv = b[index][0] if index < len(b) else None
            if av != bv:
                def show(value):
                    return "missing" if value is None else f"0x{value:04x}"
                raise ValueError(f"first SPU divergence at VBlank {vb}, register 0x{reg:03x}, "
                                 f"write {index}: port={show(av)} reference={show(bv)}")
            delta = abs(a[index][1] - b[index][1])
            maximum = max(maximum, delta)
            total += delta
            count += 1
    return dict(writes=count, sample_offset_max=maximum, sample_offset_mean=total / count if count else 0)


def trace_diagnostics(actual, reference):
    rule_b = diagnostic(compare_trace, actual, reference)
    # Pair each register's occurrences in original order, irrespective of VBlank.
    # Report unmatched occurrences explicitly; this does not accept a prefix.
    sides = []
    for path in (actual, reference):
        registers = defaultdict(list)
        for _, sample, reg, value in trace_data(path)[0]:
            registers[reg].append((sample, value))
        sides.append(registers)
    offsets, unpaired = [], [0, 0]
    for reg in sides[0].keys() | sides[1].keys():
        a, b = sides[0][reg], sides[1][reg]
        offsets.extend(abs(x[0]-y[0]) for x, y in zip(a, b))
        unpaired[0] += max(0, len(a)-len(b))
        unpaired[1] += max(0, len(b)-len(a))
    return dict(rule_b=rule_b, sample_pairing="register occurrence order, across VBlanks",
                paired_writes=len(offsets), unpaired_writes=unpaired,
                sample_offset_max=max(offsets, default=0),
                sample_offset_mean=sum(offsets)/len(offsets) if offsets else 0)


def read_pcm(path):
    with wave.open(str(path), "rb") as stream:
        if (stream.getnchannels(), stream.getsampwidth(), stream.getframerate(),
                stream.getcomptype()) != (2, 2, RATE, "NONE"):
            raise ValueError(f"expected 44.1 kHz stereo PCM16 WAV: {path}")
        count = stream.getnframes()
        data = stream.readframes(count)
    if not count or len(data) != count * 4:
        raise ValueError(f"empty or truncated WAV: {path}")
    pcm = array("h")
    pcm.frombytes(data)
    if sys.byteorder != "little":
        pcm.byteswap()
    return pcm


def fft(values, inverse=False):
    """Radix-2 FFT, used only to find one global alignment for each segment."""
    n = len(values)
    j = 0
    for i in range(1, n):
        bit = n >> 1
        while j & bit:
            j ^= bit
            bit >>= 1
        j ^= bit
        if i < j:
            values[i], values[j] = values[j], values[i]
    size = 2
    while size <= n:
        step = cmath.exp((2j if inverse else -2j) * math.pi / size)
        half = size // 2
        for start in range(0, n, size):
            w = 1
            for i in range(start, start + half):
                even, odd = values[i], values[i + half] * w
                values[i], values[i + half] = even + odd, even - odd
                w *= step
        size *= 2
    if inverse:
        for i in range(n):
            values[i] /= n


def alignment(actual, reference, max_offset=RATE):
    """Positive lag means actual is delayed. Search both channels jointly.

    Select the highest-energy one-second reference excerpt away from the edges.
    Correlation uses the original samples, without gain correction or resampling.
    """
    frames = min(len(actual), len(reference)) // 2
    margin = min(max_offset, frames // 4)
    if margin == 0:
        return 0
    length = min(RATE, frames - 2 * margin)
    if length <= 0:
        raise ValueError("PCM too short for correlation")
    starts = range(margin, frames - margin - length + 1, length)
    start = max(starts, key=lambda s: sum(v * v for v in reference[s * 2:(s + length) * 2]))
    excerpt = reference[start * 2:(start + length) * 2]
    if not any(excerpt):
        if any(reference):
            raise ValueError("no signal in alignment search interval")
        # Silent reference still undergoes SNR comparison: any actual signal fails.
        return 0
    n = 1 << (2 * length + 2 * margin - 1).bit_length()
    correlation = [0.0] * (2 * margin + 1)
    for channel in range(2):
        a = [complex(v) for v in reference[start * 2 + channel:(start + length) * 2:2]][::-1]
        b = [complex(v) for v in actual[(start - margin) * 2 + channel:(start + length + margin) * 2:2]]
        a += [0j] * (n - len(a))
        b += [0j] * (n - len(b))
        fft(a)
        fft(b)
        for i in range(n):
            a[i] *= b[i]
        fft(a, inverse=True)
        for i in range(len(correlation)):
            correlation[i] += a[length - 1 + i].real
    # Prefer zero/small offsets on equal peaks (e.g. periodic test signals).
    maximum = max(correlation)
    tolerance = max(abs(maximum), 1) * 1e-9
    peak = min((i for i, value in enumerate(correlation) if value >= maximum - tolerance),
               key=lambda i: (abs(i - margin), i))
    return peak - margin


def compare_pcm(actual_path, reference_path, threshold=40.0, max_offset=RATE, diagnostic_only=False):
    actual, reference = read_pcm(actual_path), read_pcm(reference_path)
    lag = alignment(actual, reference, max_offset)
    a0, b0 = max(0, lag), max(0, -lag)
    frames = min(len(actual) // 2 - a0, len(reference) // 2 - b0)
    if frames < WINDOW:
        raise ValueError("aligned PCM must contain at least one 5 s window")
    # Allow only the boundary loss implied by alignment; never hide dropped samples.
    duration_mismatch = abs(len(actual) // 2 - len(reference) // 2) > abs(lag) + 1
    if duration_mismatch and not diagnostic_only:
        raise ValueError("PCM duration mismatch exceeds alignment boundary loss")
    windows = []
    for start in range(0, frames, WINDOW):
        end = min(start + WINDOW, frames)
        signal = error = 0
        for i in range(start * 2, end * 2):
            a, b = actual[a0 * 2 + i], reference[b0 * 2 + i]
            signal += b * b
            error += (a - b) ** 2
        snr = math.inf if not error else 10 * math.log10(signal / error) if signal else -math.inf
        windows.append(dict(start_sample=start, frames=end - start, snr_db=snr))
    worst = min(windows, key=lambda w: w["snr_db"])
    if worst["snr_db"] < threshold and not diagnostic_only:
        raise ValueError(f"PCM worst window {worst['start_sample'] / RATE:.3f} s: "
                         f"SNR {worst['snr_db']:.3f} dB < {threshold:g} dB; lag={lag} samples")
    return dict(lag_samples=lag, compared_frames=frames, worst_snr_db=worst["snr_db"],
                duration_mismatch=duration_mismatch, meets_40_db=worst["snr_db"] >= threshold, windows=windows)


def validate_segments(scene):
    segments = scene.get("audio", [])
    checkpoints = scene.get("checkpoints", [])
    if len(segments) != 2:
        raise ValueError("audio requires exactly two disjoint segments")
    previous = -1
    for segment in segments:
        if set(segment) - {"anchor"} not in ({"start", "finish"}, {"start", "vblanks"}, {"start", "stop_on"}):
            raise ValueError("segment requires start and exactly one of finish/vblanks/stop_on")
        if "anchor" in segment and (segment["anchor"] != "note_on" or "stop_on" not in segment):
            raise ValueError("note_on anchor requires stop_on")
        if segment["start"] not in checkpoints:
            raise ValueError("unknown segment start checkpoint")
        start = checkpoints.index(segment["start"])
        if start <= previous:
            raise ValueError("overlapping or unordered audio segments")
        if "finish" in segment:
            if segment["finish"] not in checkpoints or checkpoints.index(segment["finish"]) <= start:
                raise ValueError("invalid segment finish checkpoint")
            previous = checkpoints.index(segment["finish"])
        elif "stop_on" in segment:
            stops = segment["stop_on"]
            if (not isinstance(stops, list) or not stops
                    or any(not isinstance(name, str) or not name.isidentifier() for name in stops)
                    or len(stops) != len(set(stops))):
                raise ValueError("stop_on requires unique function symbols")
            previous = len(checkpoints)
        else:
            if type(segment["vblanks"]) is not int or not 600 <= segment["vblanks"] <= 40000:
                raise ValueError("segment vblanks must be between 600 and 40000")
            previous = len(checkpoints)
    # No input may be scheduled in the music segment, including held movement.
    final = checkpoints.index(segments[-1]["start"])
    if any(checkpoints.index(event["after"]) >= final for event in scene.get("inputs", [])):
        raise ValueError("new inputs in music segment")
    if scene.get("inputs") and scene["inputs"][-1]["buttons"]:
        raise ValueError("music segment must begin with released inputs")
    return segments


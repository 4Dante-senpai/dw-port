"""SPU trace rule B and stereo PCM comparison; no third-party dependencies."""
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


def read_trace(path):
    groups = defaultdict(list)
    previous = (-1, -1)
    with Path(path).open() as stream:
        if stream.readline().strip() != "# dw1-spu-trace-v1":
            raise ValueError(f"invalid SPU trace header: {path}")
        for line_number, line in enumerate(stream, 2):
            fields = line.split()
            try:
                if len(fields) != 4:
                    raise ValueError()
                vb, sample = map(int, fields[:2])
                reg, value = (int(field, 16) for field in fields[2:])
                if (vb < 0 or sample < 0 or vb < previous[0] or sample < previous[1]
                        or not 0 <= reg < 0x200 or reg % 2 or not 0 <= value <= 65535):
                    raise ValueError()
            except ValueError:
                raise ValueError(f"invalid SPU trace row: {path}:{line_number}") from None
            groups[vb, reg].append((value, sample))
            previous = vb, sample
    return groups


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


def compare_pcm(actual_path, reference_path, threshold=40.0, max_offset=RATE):
    actual, reference = read_pcm(actual_path), read_pcm(reference_path)
    lag = alignment(actual, reference, max_offset)
    a0, b0 = max(0, lag), max(0, -lag)
    frames = min(len(actual) // 2 - a0, len(reference) // 2 - b0)
    if frames < WINDOW:
        raise ValueError("aligned PCM must contain at least one 5 s window")
    # Allow only the boundary loss implied by alignment; never hide dropped samples.
    if abs(len(actual) // 2 - len(reference) // 2) > abs(lag) + 1:
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
    if worst["snr_db"] < threshold:
        raise ValueError(f"PCM worst window {worst['start_sample'] / RATE:.3f} s: "
                         f"SNR {worst['snr_db']:.3f} dB < {threshold:g} dB; lag={lag} samples")
    return dict(lag_samples=lag, compared_frames=frames, worst_snr_db=worst["snr_db"], windows=windows)


def validate_segments(scene):
    segments = scene.get("audio", [])
    checkpoints = scene.get("checkpoints", [])
    if len(segments) != 2:
        raise ValueError("audio requires exactly two disjoint segments")
    previous = -1
    for segment in segments:
        if set(segment) not in ({"start", "finish"}, {"start", "vblanks"}):
            raise ValueError("segment requires start and exactly one of finish/vblanks")
        if segment["start"] not in checkpoints:
            raise ValueError("unknown segment start checkpoint")
        start = checkpoints.index(segment["start"])
        if start <= previous:
            raise ValueError("overlapping or unordered audio segments")
        if "finish" in segment:
            if segment["finish"] not in checkpoints or checkpoints.index(segment["finish"]) <= start:
                raise ValueError("invalid segment finish checkpoint")
            previous = checkpoints.index(segment["finish"])
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


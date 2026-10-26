#!/usr/bin/env python3
"""Class B: ten minutes paced by the game's virtual VBlank clock, from the host anchor."""
import os
from pathlib import Path
import re
import subprocess
import sys


def verify(log, expected_numerator, expected_denominator, expected_vblanks, minimum_seconds=600):
    match = re.search(r'wall vblanks=(\d+) nanoseconds=(\d+) resynchronizations=(\d+) '
                      r'rate_num=(\d+) rate_den=(\d+)', log)
    if not match:
        raise ValueError('missing wall measurement from pacing anchor')
    vblanks, nanoseconds, resyncs, numerator, denominator = map(int, match.groups())
    if not numerator or not denominator or not nanoseconds:
        raise ValueError('invalid wall measurement')
    if (numerator, denominator, vblanks) != (expected_numerator, expected_denominator, expected_vblanks):
        raise ValueError('reported wall rate or VBlank count differs from clock.h and requested limit')
    # Keep the strict < 1 boundary exact; no float rounding at the acceptance threshold.
    error = abs(vblanks * denominator * 10**9 - nanoseconds * numerator)
    print(f'native_wall: vblanks={vblanks} wall={nanoseconds / 1e9:.9f}s '
          f'expected={nanoseconds * numerator / denominator / 1e9:.9f} '
          f'resynchronizations={resyncs}')
    if nanoseconds < minimum_seconds * 10**9 or error >= denominator * 10**9 or resyncs:
        raise ValueError(f'wall pacing failed: vblanks={vblanks}, wall_ns={nanoseconds}, '
                         f'rate={numerator}/{denominator}, resynchronizations={resyncs}')


def main():
    binary, root = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()
    pack = binary.parent / 'us.pack'
    if not pack.is_file():
        disc = os.environ.get('DW1_DISC_DIR')
        cues = sorted(Path(disc).glob('*.cue')) if disc else []
        if not cues:
            print('native_wall_10min: no Pack or disc; skipped')
            return 77
        subprocess.run([str(binary.parent / 'dw1-pack'), 'build', str(cues[0]),
                        str(root / 'tools/port/manifest/us.tsv'), str(pack)], check=True)
    header = (root / 'port/psyq/clock.h').read_text()
    numerator = int(re.search(r'#define CLOCK_VBLANK_DEN (\d+)u', header)[1])
    denominator = int(re.search(r'#define CLOCK_VBLANK_NUM (\d+)u', header)[1])
    count = (600 * numerator + denominator - 1) // denominator
    try:
        run = subprocess.run([str(binary), '--pack', str(pack), '--vblanks', str(count)],
                             env=dict(os.environ, SDL_VIDEODRIVER='offscreen', SDL_AUDIODRIVER='dummy'),
                             capture_output=True, text=True, timeout=850)
    except subprocess.TimeoutExpired as error:
        for output in (error.stdout, error.stderr):
            if output:
                print(output.decode(errors='replace') if isinstance(output, bytes) else output)
        raise ValueError('game timed out before the final wall measurement') from error
    log = run.stdout + run.stderr
    print(log)
    if run.returncode:
        raise ValueError(f'game failed: exit {run.returncode}')
    if f'reached VBlank {count} after frame 0' not in log:
        raise ValueError(f'missing exact VBlank limit {count}')
    verify(log, numerator, denominator, count)
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print(f'native_wall_10min: {error}', file=sys.stderr)
        sys.exit(1)

#!/usr/bin/env python3
"""Check ten virtual seconds of SDL wall pacing and audio ring overflow."""
import os
import re
import subprocess
import sys
import time
import tempfile

start = time.monotonic()
run = subprocess.run([sys.argv[1], '--frames', '300'],
                     env=dict(os.environ, SDL_VIDEODRIVER='offscreen', SDL_AUDIODRIVER='dummy'),
                     capture_output=True, text=True, timeout=25)
wall = time.monotonic() - start
log = run.stdout + run.stderr
print(log)
match = re.search(r'reached frame \d+ at cycle (\d+)', log)
assert run.returncode == 0 and match, f'game failed: {run.returncode}'
virtual = int(match[1]) / 33868800
assert 9 <= virtual <= 12, f'expected about 10 virtual seconds, got {virtual}'
ratio = wall / virtual
assert .95 <= ratio <= 1.10, f'wall={wall:.3f}s virtual={virtual:.3f}s ratio={ratio:.4f}'
counters = re.search(r'audio_rate overflows=(\d+) underruns=(\d+)', log)
assert counters and int(counters[1]) == 0 and int(counters[2]) == 0, \
    f'missing audio counters, ring overflow or underrun: {counters and counters[0]}'
print(f'sdl_pacing: wall={wall:.3f}s virtual={virtual:.3f}s ratio={ratio:.4f}; {counters[0]}')

# The synthetic main has no scene instrumentation, so its valid symbol is never
# marked and intentionally ends with status 1 at the frame limit. Even
# before its mark, a requested capture must bypass all wall waits.
with tempfile.TemporaryDirectory() as directory:
    start = time.monotonic()
    capture = subprocess.run([sys.argv[1], '--frames', '300',
                              '--capture-mark', 'dw1_game_main', '--capture-vblank', '1',
                              '--capture', directory + '/unused.ppm'],
                             env=dict(os.environ, SDL_VIDEODRIVER='offscreen', SDL_AUDIODRIVER='dummy'),
                             capture_output=True, text=True, timeout=10)
    elapsed = time.monotonic() - start
    assert capture.returncode == 1, capture.stdout + capture.stderr
    assert match[0] in capture.stdout, 'capture changed virtual progress'
    assert elapsed < virtual * .5, f'capture waited for wall time: {elapsed:.3f}s'
    print(f'sdl_pacing: capture bypassed wall pacing ({elapsed:.3f}s)')

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

# Exercise the production resynchronization branch with and without a real stall.
for stalled in (False, True):
    env = dict(os.environ, SDL_VIDEODRIVER='offscreen', SDL_AUDIODRIVER='dummy')
    env.pop('DW1_TEST_HOST_STALL', None)
    if stalled:
        env['DW1_TEST_HOST_STALL'] = '1'
    run = subprocess.run([sys.argv[1], '--vblanks', '30'], env=env,
                         capture_output=True, text=True, timeout=5)
    log = run.stdout + run.stderr
    print(log)
    counters = re.search(r'wall vblanks=30 nanoseconds=\d+ resynchronizations=(\d+)', log)
    assert run.returncode == 0 and counters, f'missing wall counters: {log}'
    count = int(counters[1])
    assert count == int(stalled), f'expected {int(stalled)} resynchronizations: {log}'
    assert log.count('pacing resynchronization vblank=') == count, log

# The title transition combines a full-screen texture/fade with fine-grained
# VSync polling. Repeatedly scanning every event slot makes this workload miss
# wall deadlines even without an audio wait; keep it covered without disc data.
env = dict(os.environ, SDL_VIDEODRIVER='offscreen', SDL_AUDIODRIVER='dummy',
           DW1_TEST_GPU_LOAD='1')
env.pop('DW1_TEST_HOST_STALL', None)
run = subprocess.run([sys.argv[1], '--vblanks', '120'], env=env,
                     capture_output=True, text=True, timeout=5)
log = run.stdout + run.stderr
print(log)
assert run.returncode == 0, log
wall = re.search(r'wall vblanks=120 nanoseconds=(\d+) resynchronizations=0 '
                 r'rate_num=(\d+) rate_den=(\d+)', log)
assert wall, log
nanoseconds, rate_num, rate_den = map(int, wall.groups())
assert abs(120 * rate_den * 10**9 - nanoseconds * rate_num) < rate_den * 10**9, \
    f'wall error exceeds one VBlank: {log}'
assert 'audio_rate overflows=0 underruns=0' in log, log
assert 'pacing resynchronization vblank=' not in log, log
print('sdl_pacing: textured title/fade and VSync polling keep wall deadlines')

#!/usr/bin/env python3
"""Seeded weighted inputs, following a fixed scene or recorded prefix. No game AI."""
import argparse
import random
import shutil
import subprocess
import tomllib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
BUTTON_MASKS = (0, 1 << 14, 1 << 13, 1 << 12, 1 << 3, 1 << 4, 1 << 5, 1 << 6, 1 << 7)
BUTTON_WEIGHTS = (18, 30, 5, 3, 1, 10, 10, 10, 10)
HOLD_MIN = 8
HOLD_MAX = 90
RELEASE_MIN = 8
RELEASE_MAX = 30


def read_inputs(path):
    events = []
    for line in Path(path).read_text().splitlines():
        fields = line.split()
        if len(fields) != 2:
            raise ValueError('expected VBlank and hex mask')
        k, mask = int(fields[0]), int(fields[1], 16)
        if not 0 <= k <= 0xffffffff or not 0 <= mask <= 0xffff or (events and k <= events[-1][0]):
            raise ValueError('inputs must have increasing VBlanks and 16-bit masks')
        events.append((k, mask))
    return events


def write_inputs(path, events):
    Path(path).write_text(''.join(f'{k} {mask:04x}\n' for k, mask in events))


def resolve_prefix(path, binary, pack, work, card=None, launch=None):
    """Let the existing scene host resolve symbol-relative inputs once, before random input.

    The bot never reads game memory. The resulting prefix is an ordinary frame0 recording.
    """
    path, work = Path(path), Path(work)
    work.mkdir(parents=True, exist_ok=True)
    if path.suffix != '.toml':
        events = read_inputs(path)
        return events, (events[-1][0] + 1 if events else 0)
    scene = tomllib.loads(path.read_text())
    events = scene.get('inputs', [])
    if not scene.get('mark') and not any('after' in event for event in events) and scene.get('inputs_from', 'frame0') == 'frame0':
        return [(e['vblank'], sum(1 << bit for bit in e['buttons'])) for e in events], scene.get('capture_vblank', 0)
    inputs, checkpoints = work / 'scene.inputs', work / 'checkpoints.txt'
    names = scene.get('checkpoints', [])
    inputs.write_text(''.join((e['after'] + ' ' if names else '') +
                             f"{e['vblank']} {sum(1 << bit for bit in e['buttons']):04x}\n" for e in events))
    checkpoints.write_text(''.join(f"{name} {scene.get('window', 0)}\n" for name in names))
    record, digest = work / 'prefix.inputs', work / 'digest.txt'
    cmd = [str(binary), '--headless', '--frames', '40000', '--pack', str(pack), '--inputs', str(inputs),
           '--inputs-from', scene.get('inputs_from', 'frame0'),
           '--capture-mark', scene['mark'], '--capture-vblank', str(scene['capture_vblank']),
           '--capture', str(work / 'prefix.ppm'), '--record-inputs', str(record), '--digest', str(digest)]
    if names:
        cmd += ['--checkpoints', str(checkpoints)]
    if card:
        shutil.copyfile(card, work / 'input.mcd')
        shutil.copyfile(card, work / 'working.mcd')
        cmd += ['--card1', str(work / 'working.mcd')]
    if launch:
        launch(cmd, work, inputs)
        return read_inputs(record), int(digest.read_text().splitlines()[-1].split()[0])
    result = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=600)
    (work / 'stdout.txt').write_bytes(result.stdout)
    (work / 'stderr.txt').write_bytes(result.stderr)
    if result.returncode:
        raise ValueError(f'prefix host failed ({result.returncode}); see {work}/stderr.txt')
    return read_inputs(record), int(digest.read_text().splitlines()[-1].split()[0])


def generate(seed, vblanks, prefix=(), prefix_end=0):
    rng = random.Random(seed)
    result = [(k, mask) for k, mask in prefix if k <= vblanks]
    k = max(prefix_end + 1, result[-1][0] + 1 if result else 0)
    mask = result[-1][1] if result else 0
    while k <= vblanks:
        value = rng.choices(BUTTON_MASKS, weights=BUTTON_WEIGHTS)[0]
        if value != mask:
            result.append((k, value))
            mask = value
        k += rng.randint(HOLD_MIN, HOLD_MAX)
        if mask and k <= vblanks:
            result.append((k, 0))
            mask = 0
        k += rng.randint(RELEASE_MIN, RELEASE_MAX)
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--seed', type=int, required=True)
    p.add_argument('--vblanks', type=int, required=True)
    p.add_argument('--prefix', type=Path)
    p.add_argument('--build', type=Path, default=ROOT / 'build/port/native-headless/dw1')
    p.add_argument('--pack', type=Path, default=ROOT / 'build/port/native-headless/us.pack')
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    if a.vblanks <= 0:
        p.error('--vblanks must be positive')
    prefix, end = resolve_prefix(a.prefix, a.build.resolve(), a.pack.resolve(), a.output.parent / 'prefix') if a.prefix else ([], 0)
    write_inputs(a.output, generate(a.seed, a.vblanks, prefix, end))


if __name__ == '__main__':
    main()

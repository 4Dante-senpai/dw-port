#!/usr/bin/env python3
"""Run deterministic playtests, detect failures, and keep private reproductions."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import importlib.util
import itertools
import json
import math
import os
from pathlib import Path
import re
import shlex
import shutil
import signal
import subprocess
import sys
import time

_spec = importlib.util.spec_from_file_location('playtest_bot', Path(__file__).with_name('bot.py'))
bot = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(bot)
ROOT = Path(__file__).resolve().parents[3]
RATE = 60.0087172527
DIGEST_LINE = re.compile(r'^(\d+) (\d+) (off|[0-9a-f]{64}) ([0-9a-f]{64})$')


def digest_rows(path):
    rows = []
    if Path(path).exists():
        for line in Path(path).read_text().splitlines():
            match = DIGEST_LINE.fullmatch(line)
            if match:
                rows.append((int(match[1]), int(match[2]), match[3], match[4]))
    return rows


def classify(code, stderr, hung=False):
    for kind, pattern in [('sanitizer', r'.*(?:ERROR: AddressSanitizer|runtime error:).*'),
                          ('assert', r'.*PORT_ASSERT.*')]:
        match = re.search(pattern, stderr)
        if match:
            return kind, match[0]
    if hung:
        return 'hang', 'digest stopped advancing'
    if code == 2:
        return 'usage', 'host rejected its arguments or inputs'
    if code:
        return 'crash', f'signal {-code}' if code < 0 else f'exit {code}'
    return None, None


def execute(binary, pack, inputs, work, vblanks, card=None, hang_seconds=60, frozen_vblanks=600, seed=None):
    work = Path(work).resolve()
    work.mkdir(parents=True, exist_ok=True)
    inputs = Path(inputs).resolve()
    bot.read_inputs(inputs)  # fail before starting the game
    replay = work / 'inputs.txt'
    shutil.copyfile(inputs, replay)
    cmd = [str(Path(binary).resolve()), '--headless', '--pack', str(Path(pack).resolve()), '--inputs', str(replay),
           '--inputs-from', 'frame0', '--vblanks', str(vblanks), '--digest', str(work / 'digest.txt'),
           '--digest-every', '60']
    if card:
        shutil.copyfile(card, work / 'input.mcd')
        shutil.copyfile(work / 'input.mcd', work / 'working.mcd')
        cmd += ['--card1', str(work / 'working.mcd')]
    return observe(cmd, work, vblanks, hang_seconds, frozen_vblanks, seed, replay)


def observe(cmd, work, vblanks, hang_seconds, frozen_vblanks, seed, replay):
    """Monitor both scene-prefix resolution and ordinary frame0 replay."""
    card = '--card1' in cmd
    # Reproductions always restore the pristine card, even after repeated runs.
    script = '#!/bin/sh\nset -eu\n'
    if card:
        script += 'cp ' + shlex.quote(str(work / 'input.mcd')) + ' ' + shlex.quote(str(work / 'working.mcd')) + '\n'
    script += 'export ASAN_OPTIONS=' + shlex.quote(os.environ.get('ASAN_OPTIONS', '') + ':detect_stack_use_after_return=0') + '\n'
    script += 'export DW1_TRACE=' + shlex.quote(str(work / 'host.trace')) + '\n'
    script += 'export DW1_TIMING_TRACE=' + shlex.quote(str(work / 'timing.jsonl')) + '\n'
    script += 'exec ' + shlex.join(cmd) + '\n'
    (work / 'reproduce.sh').write_text(script)
    (work / 'reproduce.sh').chmod(0o700)
    start = last_progress = time.monotonic()
    last_k = -1
    hung = False
    digest = work / 'digest.txt'
    digest.unlink(missing_ok=True)
    with (work / 'stdout.txt').open('wb') as stdout, (work / 'stderr.txt').open('wb') as stderr:
        env = dict(os.environ, DW1_TRACE=str(work / 'host.trace'), DW1_TIMING_TRACE=str(work / 'timing.jsonl'))
        env['ASAN_OPTIONS'] = env.get('ASAN_OPTIONS', '') + ':detect_stack_use_after_return=0'
        process = subprocess.Popen(cmd, stdout=stdout, stderr=stderr, start_new_session=True, env=env)
        # Read each digest line once, including for multi-hour soaks.
        offset, partial, rows = 0, '', []
        while True:
            if digest.exists():
                with digest.open() as stream:
                    stream.seek(offset)
                    chunk = stream.read()
                    offset = stream.tell()
                lines = (partial + chunk).split('\n')
                partial = lines.pop()
                for line in lines:
                    match = DIGEST_LINE.fullmatch(line)
                    if match:
                        row = (int(match[1]), int(match[2]), match[3], match[4])
                        rows.append(row)
                        if row[0] > last_k:
                            last_k, last_progress = row[0], time.monotonic()
            code = process.poll()
            if code is not None:
                # The process may have flushed its last line between the read and poll.
                rows = digest_rows(digest)
                break
            if time.monotonic() - last_progress > hang_seconds:
                hung = True
                os.killpg(process.pid, signal.SIGKILL)
                code = process.wait()
                rows = digest_rows(digest)
                break
            time.sleep(min(0.05, hang_seconds / 10))
    with (work / 'stderr.txt').open('rb') as stream:
        stream.seek(max(0, stream.seek(0, 2) - 8192))
        tail = stream.read().decode(errors='replace')
    # Search the full stream for the first diagnostic, keeping reports bounded.
    first = ''
    overlays = set()
    with (work / 'stderr.txt').open(errors='replace') as stream:
        for line in stream:
            visit = re.search(r'playtest: overlay (\d+) at VBlank', line)
            if visit:
                overlays.add(int(visit[1]))
            if not first and re.search(r'ERROR: AddressSanitizer|runtime error:|PORT_ASSERT', line):
                first = line.strip()
    kind, detail = classify(code, first or tail, hung)
    final = rows[-1] if rows else None
    if not kind and (not final or (vblanks is not None and final[0] != vblanks)):
        kind, detail = 'crash', 'successful exit without the requested final digest'
    warnings = []
    unchanged = 0
    previous = None
    for k, _, display, _ in rows:
        if display == 'off':
            if 'display disabled' not in warnings:
                warnings.append('display disabled')
        if previous and display == previous[2]:
            unchanged += k - previous[0]
            if unchanged > frozen_vblanks and 'frozen display' not in warnings:
                warnings.append('frozen display')
        else:
            unchanged = 0
        previous = (k, 0, display)
    elapsed = time.monotonic() - start
    result = dict(seed=seed, status=kind or 'ok', detail=detail, exit_code=code,
                  vblanks=final[0] if final else 0, digest=final[3] if final else None,
                  overlays=sorted(overlays | {r[1] for r in rows if r[1]}), warnings=warnings,
                  virtual_hours=(final[0] if final else 0) / RATE / 3600,
                  wall_seconds=elapsed, vblanks_per_second=(final[0] if final else 0) / elapsed,
                  reproduction=str(work / 'reproduce.sh'),
                  inputs_sha256=hashlib.sha256(replay.read_bytes()).hexdigest())
    if kind:
        result['stderr_tail'] = tail
        print(f'{kind}: {detail}; VBlank {result["vblanks"]}; reproduce: {work / "reproduce.sh"}', flush=True)
    return result


def compare(results):
    return len({(r['vblanks'], r['digest']) for r in results if r['status'] == 'ok'}) <= 1


def first_difference(results):
    """Locate the first sampled interval that differs, keeping traces private."""
    successful = [r for r in results if r['status'] == 'ok']
    if len(successful) < 2:
        return None
    traces = [dict((row[0], row[3]) for row in
                   digest_rows(Path(r['reproduction']).parent / 'digest.txt'))
              for r in successful]
    previous = None
    for vblank in sorted(set().union(*traces)):
        if len({trace.get(vblank) for trace in traces}) != 1:
            return dict(after_vblank=previous, through_vblank=vblank,
                        builds=[r['build'] for r in successful])
        previous = vblank
    return None


def smoke_builds(builds):
    """Compare the standard Debug/optimized pair when both binaries exist."""
    pair = [ROOT / 'build/port' / name / 'dw1'
            for name in ('native-headless', 'native-headless-release')]
    resolved = [Path(b).resolve() if '/' in b else ROOT / 'build/port' / b / 'dw1'
                for b in builds]
    standard = [*pair, ROOT / 'build/port/native-asan/dw1']
    if all(binary.is_file() for binary in pair) and any(binary in standard for binary in resolved):
        for binary in pair:
            if binary not in resolved:
                builds = [*builds, str(binary)]
    return builds


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build', action='append', default=[])
    p.add_argument('--pack', type=Path)
    p.add_argument('--inputs', type=Path)
    p.add_argument('--session', type=Path, action='append', default=[])
    p.add_argument('--prefix', type=Path, action='append', default=[])
    p.add_argument('--card', type=Path, action='append', default=[])
    p.add_argument('--seed', type=int, action='append', default=[])
    p.add_argument('--vblanks', type=int, default=15000)
    p.add_argument('--jobs', type=int, default=1)
    p.add_argument('--hang-seconds', type=float, default=60)
    p.add_argument('--frozen-vblanks', type=int, default=600)
    p.add_argument('--output', type=Path, default=ROOT / 'build/port/playtest/latest')
    p.add_argument('--smoke', action='store_true')
    p.add_argument('--soak', action='store_true')
    a = p.parse_args(argv)
    out = a.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    for name in ('report.json', 'summary.json'):
        (out / name).unlink(missing_ok=True)
    if a.smoke and not os.environ.get('DW1_DISC_DIR'):
        print('playtest_smoke: DW1_DISC_DIR not set; skipped')
        return 77
    if a.vblanks <= 0 or a.jobs <= 0 or not math.isfinite(a.hang_seconds) or a.hang_seconds <= 0 or a.frozen_vblanks < 0:
        p.error('limits must be positive')
    builds = a.build or ['native-headless']
    if a.smoke:
        builds = smoke_builds(builds)
    binaries = [Path(b).resolve() if '/' in b else ROOT / 'build/port' / b / 'dw1' for b in builds]
    pack = (a.pack or binaries[0].parent / 'us.pack').resolve()
    sources = ([(a.inputs, True)] if a.inputs else []) + [(path, False) for path in a.session + a.prefix]
    if not sources:
        sources = [(ROOT / 'tools/port/scenes/newgame.toml' if a.smoke else None, False)]
    seeds = a.seed or ([1, 2, 3, 4] if a.soak else [1])
    tasks = list(itertools.product(seeds, sources, a.card or [None]))

    def trial(index_task):
        index, (seed, (prefix, verbatim), card) = index_task
        work = out / f'trial-{index:04d}'
        work.mkdir(parents=True, exist_ok=True)
        pristine = None
        if card:
            pristine = work / 'prefix-input.mcd'
            shutil.copyfile(card, pristine)
        class PrefixFailure(Exception):
            def __init__(self, result):
                self.result = result

        def launch_prefix(cmd, directory, record):
            result = observe(cmd, directory, None, a.hang_seconds, a.frozen_vblanks, seed, record)
            if result['status'] != 'ok':
                result['build'] = builds[0]
                raise PrefixFailure(result)
        try:
            events, end = bot.resolve_prefix(prefix, binaries[0], pack, work / 'prefix', pristine,
                                             launch=launch_prefix) if prefix else ([], 0)
        except PrefixFailure as error:
            return dict(seed=seed, runs=[error.result], digests_match=True)
        limit = max(a.vblanks, end + 600) if a.smoke else a.vblanks
        # --inputs replays the exact session; --session extends it with random input for soak.
        inputs = work / 'generated.txt'
        bot.write_inputs(inputs, events if verbatim else bot.generate(seed, limit, events, end))
        results = []
        for n, binary in enumerate(binaries):
            result = execute(binary, pack, inputs, work / f'build-{n}', limit, pristine,
                             a.hang_seconds, a.frozen_vblanks, seed)
            result['build'] = builds[n]
            results.append(result)
        matches = compare(results)
        difference = first_difference(results) if not matches else None
        if not matches:
            print(f'digest mismatch in interval {difference}; final VBlank {limit}; reproduce: ' +
                  ' ; '.join(r['reproduction'] for r in results), flush=True)
        return dict(seed=seed, runs=results, digests_match=matches, first_difference=difference)

    try:
        with ThreadPoolExecutor(max_workers=a.jobs) as pool:
            trials = list(pool.map(trial, enumerate(tasks)))
    except (ValueError, OSError, subprocess.TimeoutExpired) as error:
        (out / 'report.json').write_text(json.dumps({'status': 'usage', 'detail': str(error)}, indent=2) + '\n')
        print(f'playtest: {error}', file=sys.stderr)
        return 2
    runs = [run for trial in trials for run in trial['runs']]
    failures = sum(r['status'] != 'ok' for r in runs) + sum(not t['digests_match'] for t in trials)
    report = dict(status='failed' if failures else 'ok', failures=failures,
                  virtual_hours=sum(r['virtual_hours'] for r in runs),
                  overlays=sorted({o for r in runs for o in r['overlays']}), trials=trials)
    (out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    public = {k: report[k] for k in ('status', 'failures', 'virtual_hours', 'overlays')}
    public['runs'] = [{k: r[k] for k in ('seed', 'digest', 'vblanks', 'virtual_hours', 'overlays', 'inputs_sha256')} for r in runs]
    (out / 'summary.json').write_text(json.dumps(public, indent=2) + '\n')
    print(f'playtest: {len(runs)} runs, {failures} failures, {report["virtual_hours"]:.6f} virtual hours; {out / "report.json"}')
    return 2 if any(r['status'] == 'usage' for r in runs) else int(bool(failures))


if __name__ == '__main__':
    sys.exit(main())

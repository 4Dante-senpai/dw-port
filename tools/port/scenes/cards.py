#!/usr/bin/env python3
"""Build external native memory-card, absolute replay and frame fixtures (no Redux)."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tomllib

ROOT = Path(__file__).resolve().parents[3]
AFTER_LOAD_VBLANKS = 600
spec = importlib.util.spec_from_file_location('scene_run', Path(__file__).with_name('run.py'))
scene_run = importlib.util.module_from_spec(spec)
spec.loader.exec_module(scene_run)


def capture(scene_id, pack, output, source=None, replayed=None, stem=None):
    scene = tomllib.loads((ROOT / 'tools/port/scenes' / (scene_id + '.toml')).read_text())
    scene_run.validate_scene(scene)
    # A replayed run takes the resolved inputs by mark, so it can outlive the scene's checkpoints.
    scene['card1'] = 'new'
    if source: scene['_card1_input'] = str(source)
    binary = ROOT / 'build/port/native-headless/dw1'
    checkpoints = scene_run.checkpoint_symbols(scene, ROOT / 'config/us/symbols.txt', binary)
    stem = stem or scene_id
    inputs = output / (stem + '.inputs.txt')
    inputs.write_text(''.join(f"{e['after']} {e['vblank']} {sum(1 << b for b in set(e['buttons'])):04x}\n"
                              for e in scene['inputs']))
    cp_file = output / (stem + '.checkpoints.txt')
    cp_file.write_text(''.join(f"{cp['label']} {cp['window']} {cp.get('after', '-')}\n" for cp in checkpoints))
    cards = scene_run.prepare_cards(scene, 'web-fixture', 2 if replayed else 1)
    capture_file = output / (stem + '.ppm')
    replay = output / (stem + '.replay.txt')
    clock = output / (stem + '.clock')
    trace = output / (stem + '.trace')
    capture_vblank = replayed['capture_vblank'] if replayed else scene['capture_vblank']
    if replayed:
        mark_vblank = replayed['mark_vblank']
        cp_file.write_text(f"{scene['mark']} {capture_vblank} -\n")
        inputs.write_text(''.join(f"{scene['mark']} {int(line.split()[0]) - mark_vblank} {line.split()[1]}\n"
                                  for line in (output / (scene_id + '.replay.txt')).read_text().splitlines()
                                  if int(line.split()[0]) >= mark_vblank))
    command = [str(binary), '--headless', '--pack', str(pack), '--frames', '40000',
               '--capture-mark', scene['mark'], '--capture-vblank', str(capture_vblank),
               '--capture', str(capture_file), '--inputs', str(inputs), '--inputs-from', 'mark',
               '--clock-trace', str(clock), '--resolved-inputs', str(replay),
               '--card1', str(cards[0]), '--card2', str(cards[1])]
    command += ['--checkpoints', str(cp_file)]
    result = subprocess.run(command, cwd=binary.parent, env=dict(os.environ, DW1_TRACE=str(trace)), capture_output=True, timeout=1800)
    log = result.stdout + result.stderr
    (output / (stem + '.log')).write_bytes(log)
    if result.returncode or re.search(rb'runtime error|AddressSanitizer', log):
        raise ValueError(f'{stem}: native failed ({result.returncode}); see {output}/{stem}.log')
    final = re.search(rb'final absolute VBlank (\d+)', log)
    if not final: raise ValueError('native did not report final absolute VBlank')
    if cards[0].stat().st_size != 131072: raise ValueError('invalid native card size')
    destination = output / (stem + '.mcd')
    shutil.copyfile(cards[0], destination)
    mark_cycle = int(re.search(rf"^(\d+) mark {scene['mark']}$", trace.read_text(), re.M)[1])
    mark_vblank = sum(int(cycle) <= mark_cycle and kind == '-1' for cycle, kind in
                      (line.split() for line in clock.read_text().splitlines()))
    return dict(mark=scene['mark'], capture_vblank=int(final[1]) - mark_vblank, final_vblank=int(final[1]),
                sha256=hashlib.sha256(capture_file.read_bytes()).hexdigest(),
                card_sha256=hashlib.sha256(destination.read_bytes()).hexdigest(),
                replay_sha256=hashlib.sha256(replay.read_bytes()).hexdigest())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pack', required=True, type=Path)
    parser.add_argument('--output', type=Path, default=Path(os.environ.get('DW1_CARD_DIR_WEB', '/tmp/dw1-web-cards')))
    args = parser.parse_args()
    if not os.environ.get('DW1_DISC_DIR'):
        print('native_cards: DW1_DISC_DIR not set; skipped'); return 77
    output = args.output.resolve()
    if output.is_relative_to(ROOT) or any(c.isspace() for c in str(output)):
        raise ValueError('card fixtures must be outside repository without whitespace')
    output.mkdir(parents=True, exist_ok=True)
    (output / 'fixture.json').unlink(missing_ok=True)
    pack = args.pack.resolve()
    record = {}
    record['card_newgame'] = capture('card_newgame', pack, output)
    saved = output / 'card_newgame.mcd'
    first = capture('card_continue', pack, output, saved)
    # Past loadSavegame the frame shows the loaded game, not only the menu that found the save.
    replayed = {'mark_vblank': first['final_vblank'] - first['capture_vblank'],
                'capture_vblank': first['capture_vblank'] + AFTER_LOAD_VBLANKS}
    record['card_continue'] = dict(capture('card_continue', pack, output, saved, replayed, 'card_continue-loaded'),
                                   replay_sha256=first['replay_sha256'])
    record['control'] = capture('card_continue', pack, output, None, replayed, 'card_continue-blank')
    if record['control']['sha256'] == record['card_continue']['sha256']:
        raise ValueError('Continue frame does not distinguish saved and empty card')
    (output / 'fixture.json').write_text(json.dumps(record, indent=2) + '\n')
    print(json.dumps(record)); return 0


if __name__ == '__main__':
    raise SystemExit(main())

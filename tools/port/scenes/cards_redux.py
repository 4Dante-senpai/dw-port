#!/usr/bin/env python3
"""Manual cross-load verification; every Redux run has a distinct external output."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tomllib

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location('cards_scene', Path(__file__).with_name('cards.py'))
cards = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cards)
runner = cards.scene_run


def capture(scene, output, card=None, reuse=False):
    command = [sys.executable, '-I', '-B', str(ROOT / 'tools/port/reference/capture.py'), scene,
               '--state', '--card-build', 'native-headless', '--max-vblanks', '40000', '--timeout', '7200',
               '--out', str(output)]
    if reuse: command.append('--reuse')
    if card: command += ['--card1', str(card)]
    subprocess.run(command, check=True, cwd=ROOT, env=dict(os.environ, DW1_REFERENCE_DIR=str(output.parent)))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['prepare', 'verify'])
    parser.add_argument('--card', type=Path)
    parser.add_argument('--browser', choices=['chromium', 'firefox'])
    args = parser.parse_args()
    output = Path(os.environ.get('DW1_CARD_DIR_WEB', '/tmp/dw1-web-cards')).resolve()
    if output.is_relative_to(ROOT): raise ValueError('external fixtures required')
    reference = output / 'redux'
    reference.mkdir(parents=True, exist_ok=True)
    if args.action == 'prepare':
        disc = os.environ.get('DW1_DISC_DIR')
        if not disc or not list(Path(disc).glob('*.cue')) or not shutil.which('pcsx-redux'):
            print('cards_redux: original .cue/.bin and PCSX-Redux required; skipped')
            return 77
        subprocess.run([sys.executable, str(Path(__file__).with_name('cards.py')), '--pack',
                        str(ROOT / 'build/port/web/us.pack'), '--output', str(output)], check=True)
        capture('card_newgame', reference / 'newgame', reuse=True)
        shutil.copyfile(runner.final_card('reference', 'card_newgame'), output / 'redux-newgame.mcd')
        capture('card_continue', reference / 'native-continue', card=output / 'card_newgame.mcd', reuse=True)
        return 0
    if not args.card or not args.browser: raise ValueError('--card and --browser required')
    if args.card.resolve().is_relative_to(ROOT): raise ValueError('export must be outside repository')
    destination = reference / ('export-' + args.browser)
    capture('card_continue', destination, card=args.card.resolve())
    scene = tomllib.loads((ROOT / 'tools/port/scenes/card_continue.toml').read_text())
    variables = runner.state_variables(ROOT / 'tools/port/scenes/card_state_vars.txt', ROOT / 'config/us/symbols.txt')
    checkpoints = runner.checkpoint_symbols(scene, ROOT / 'config/us/symbols.txt')
    # Compare only the payload after loadSavegame, as required by the cross-load contract.
    runner.compare_saved_state(reference / 'native-continue/run-1.state', destination / 'run-1.state',
                               variables, [cp['label'] for cp in checkpoints], scene['window'])
    evidence = {'browser':args.browser, 'native_reference':str(reference / 'native-continue/run-1.state'),
                'web_export_reference':str(destination / 'run-1.state'), 'loadSavegame_match':True}
    (reference / ('verified-' + args.browser + '.json')).write_text(json.dumps(evidence, indent=2) + '\n')
    return 0


if __name__ == '__main__': raise SystemExit(main())

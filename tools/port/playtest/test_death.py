"""Class B: the real game, killed with SIGQUIT after the newgame prefix, leaves a usable death dump.

usage: test_death.py BUILD OUTPUT   (exit 77 without DW1_DISC_DIR)
"""
import importlib.util
import os
import signal
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
AFTER_PREFIX = 120  # VBlanks past the prefix before the SIGQUIT
TIMEOUT = 1800


def load(name):
    spec = importlib.util.spec_from_file_location(name, HERE / (name + '.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main():
    if not os.environ.get('DW1_DISC_DIR'):
        print('playtest_death: DW1_DISC_DIR not set; skipped')
        return 77
    binary, out = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()
    out.mkdir(parents=True, exist_ok=True)
    pack = binary.parent / 'us.pack'
    bot, runner = load('bot'), load('run')
    events, end = bot.resolve_prefix(ROOT / 'tools/port/scenes/newgame.toml', binary, pack, out / 'prefix')
    inputs, digest, dump = out / 'inputs.txt', out / 'digest.txt', out / 'death.txt'
    bot.write_inputs(inputs, events)
    digest.unlink(missing_ok=True)
    cmd = [str(binary), '--headless', '--pack', str(pack), '--inputs', str(inputs), '--inputs-from', 'frame0',
           '--vblanks', str(end + 100000), '--digest', str(digest), '--digest-every', '60', '--death-file', str(dump)]
    with (out / 'stderr.txt').open('wb') as err:
        process = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=err, start_new_session=True)
        deadline = time.monotonic() + TIMEOUT
        try:
            while time.monotonic() < deadline and process.poll() is None:
                rows = runner.digest_rows(digest)
                if rows and rows[-1][0] >= end + AFTER_PREFIX:
                    break
                time.sleep(0.2)
            else:
                sys.exit(f'the game did not reach VBlank {end + AFTER_PREFIX} (exit {process.poll()})')
            os.kill(process.pid, signal.SIGQUIT)
            code = process.wait(runner.QUIT_GRACE)
        finally:
            if process.poll() is None:
                os.killpg(process.pid, signal.SIGKILL)
    death = runner.parse_death(dump)
    if not death or death['reason'] != 'quit':
        sys.exit(f'no quit dump (exit {code}): {dump}')
    runner.symbolize(binary, death)
    state = death['state']
    if not state or state['screen'] == 0 or state['k'] != death['vblank'] or death['vblank'] < end:
        sys.exit(f'unexpected state in the dump: {state}')
    named = [f for f in death['frames'] if f['function'] and not runner.is_noise(f)]
    if not death['symbolized'] or not named:
        sys.exit(f'no symbolized game frame: {death["frames"]}')
    name = runner.signature(None, '', death, True)
    if not name.startswith('hang:'):
        sys.exit(f'unexpected signature {name}')
    print(f'playtest_death: VBlank {death["vblank"]}, screen {state["screen"]}, overlay {state["overlay"]}, '
          f'{len(death["inputs"])} inputs, signature {name}')
    return 0


if __name__ == '__main__':
    sys.exit(main())

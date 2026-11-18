"""Class B: --seed and --digest-sparse on the real game, after the newgame prefix.

usage: test_seed_sparse.py BUILD OUTPUT   (exit 77 without DW1_DISC_DIR)
"""
import importlib.util
import os
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
AFTER_PREFIX = 600
DEFAULT_SEED = 0x24040001


def load(name):
    spec = importlib.util.spec_from_file_location(name, HERE / (name + '.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main():
    if not os.environ.get('DW1_DISC_DIR'):
        print('playtest_seed_sparse: DW1_DISC_DIR not set; skipped')
        return 77
    binary, out = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()
    out.mkdir(parents=True, exist_ok=True)
    pack = binary.parent / 'us.pack'
    bot, runner = load('bot'), load('run')
    events, end = bot.resolve_prefix(ROOT / 'tools/port/scenes/newgame.toml', binary, pack, out / 'prefix')
    inputs = out / 'inputs.txt'
    bot.write_inputs(inputs, events)
    limit = end + AFTER_PREFIX

    def run(name, **kw):
        result = runner.execute(binary, pack, inputs, out / name, limit, **kw)
        if result['status'] != 'ok' or result['vblanks'] != limit:
            sys.exit(f'{name}: {result["status"]} {result["detail"]} at VBlank {result["vblanks"]}')
        return result, runner.digest_rows(out / name / 'digest.txt')

    plain, plain_rows = run('plain')
    explicit, _ = run('explicit', rand_seed=DEFAULT_SEED)
    other, _ = run('other', rand_seed=1)
    again, _ = run('other-again', rand_seed=1)
    sparse, sparse_rows = run('sparse', digest_sparse=True)
    sparse_again, _ = run('sparse-again', digest_sparse=True)
    if plain['digest'] != explicit['digest']:
        sys.exit('--seed 0x24040001 changed the digest')
    if other['digest'] == plain['digest'] or other['digest'] != again['digest']:
        sys.exit('--seed 1 must give another, repeatable digest')
    if [r[:3] for r in plain_rows] != [r[:3] for r in sparse_rows]:
        sys.exit('sparse lines differ from dense in VBlank, overlay or screen')
    if sparse['digest'] == plain['digest'] or sparse['digest'] != sparse_again['digest']:
        sys.exit('sparse must give another, repeatable chain')
    print(f'playtest_seed_sparse: {limit} VBlanks; dense {plain["vblanks_per_second"]:.0f} VBlanks/s, '
          f'sparse {sparse["vblanks_per_second"]:.0f} VBlanks/s')
    return 0


if __name__ == '__main__':
    sys.exit(main())

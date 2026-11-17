"""Class B: the live state channel drives the real game like a recorded replay would.

usage: test_channel.py BUILD OUTPUT   (exit 77 without DW1_DISC_DIR)
Runs the newgame prefix plus 3000 VBlanks of seeded masks through --state-fd/--input-fd, then
replays the recording without a channel: same final digest, a non-zero screen id and a tamer position.
"""
import importlib.util
import os
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
EXTRA = 3000
SEED = 7


def load(name):
    spec = importlib.util.spec_from_file_location(name, HERE / (name + '.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def mask_schedule(events, total):
    """(k, mask, hold) for every change of the event list, with a leading mask 0 if needed."""
    if not events or events[0][0] > 0:
        events = [(0, 0)] + list(events)
    ends = [k for k, _ in events[1:]] + [total + 1]
    return {k: (mask, end - k) for (k, mask), end in zip(events, ends)}


def main():
    if not os.environ.get('DW1_DISC_DIR'):
        print('playtest_channel: DW1_DISC_DIR not set; skipped')
        return 77
    binary, out = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()
    out.mkdir(parents=True, exist_ok=True)
    pack = binary.parent / 'us.pack'
    bot, channel = load('bot'), load('channel')
    events, end = bot.resolve_prefix(ROOT / 'tools/port/scenes/newgame.toml', binary, pack, out / 'prefix')
    total = end + EXTRA
    schedule = mask_schedule(bot.generate(SEED, total, events, end), total)
    recorded, live_digest = out / 'recorded.txt', out / 'live.digest'
    cmd = [str(binary), '--headless', '--pack', str(pack), '--vblanks', str(total),
           '--record-inputs', str(recorded), '--digest', str(live_digest), '--digest-every', '60']
    records, code, _, err = channel.run(cmd, lambda r: schedule[r['k']])
    if code:
        sys.exit(f'lockstep run failed ({code}): {err[-2000:].decode(errors="replace")}')
    replay_digest = out / 'replay.digest'
    result = subprocess.run([str(binary), '--headless', '--pack', str(pack), '--inputs', str(recorded),
                             '--inputs-from', 'frame0', '--vblanks', str(total), '--digest', str(replay_digest),
                             '--digest-every', '60'], capture_output=True)
    if result.returncode:
        sys.exit(f'replay failed ({result.returncode}): {result.stderr[-2000:].decode(errors="replace")}')
    live, replay = live_digest.read_text().splitlines()[-1], replay_digest.read_text().splitlines()[-1]
    if live != replay:
        sys.exit(f'digest mismatch: lockstep {live}, replay {replay}')
    if [r['k'] for r in records] != list(range(total + 1)):
        sys.exit('expected one record per VBlank')
    screens = {r['screen'] for r in records}
    moved = any((r['tx'], r['ty'], r['tz']) != (0, 0, 0) for r in records if r['k'] > end)
    print(f'playtest_channel: {len(records)} records, screens {sorted(screens)}, overlays '
          f'{sorted({r["overlay"] for r in records})}, tamer moved {moved}, digest {live.split()[-1][:16]}')
    if screens == {0} or not moved:
        sys.exit('the observed state never left its initial values')


if __name__ == '__main__':
    main()

#!/usr/bin/env python3
"""Synthetic checkpoint gates: equality, divergence, missing evidence and cost diagnostics."""
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('timing', Path(__file__).with_name('timing.py'))
timing = importlib.util.module_from_spec(spec)
spec.loader.exec_module(timing)
wall_spec = importlib.util.spec_from_file_location('wall', Path(__file__).parents[1] / 'test_native_wall.py')
wall = importlib.util.module_from_spec(wall_spec)
wall_spec.loader.exec_module(wall)


class TimingTests(unittest.TestCase):
    def test_checkpoints(self):
        with tempfile.TemporaryDirectory() as directory:
            ref, port = (Path(directory) / name for name in ('ref', 'port'))
            def write(path, shift=0, missing=False, label='loadMap'):
                rows = []
                for event, name, k in [('frame0', 'frame0', 0), ('mark', 'boot', 10),
                                       ('checkpoint', label, 20 + shift)][:2 if missing else 3]:
                    costs = dict.fromkeys(timing.SOURCES, 0)
                    costs['cd'] = k * 100
                    rows.append(dict(event=event, label=name, cycle=k * 100, vblank=k, costs=costs))
                path.write_text('\n'.join(map(json.dumps, rows)) + '\n')
            write(ref); write(port)
            with contextlib.redirect_stdout(io.StringIO()) as log:
                result = timing.compare(port, ref, 'boot', ['loadMap'])
            self.assertEqual([r['diff'] for r in result], [0, 0])
            self.assertIn('checkpoint ref port diff', log.getvalue())
            for shift in (-1, 1):
                write(port, shift)
                with contextlib.redirect_stdout(io.StringIO()), self.assertRaisesRegex(
                        ValueError, r'checkpoint loadMap.*reference cycles=.*cd.*port cycles='):
                    timing.compare(port, ref, 'boot', ['loadMap'])
            for missing_side in (port, ref):
                write(port); write(ref); write(missing_side, missing=True)
                with contextlib.redirect_stdout(io.StringIO()), self.assertRaisesRegex(ValueError, 'missing checkpoint loadMap'):
                    timing.compare(port, ref, 'boot', ['loadMap'])
            write(ref, label='diagnostic'); write(port, 1, label='diagnostic')
            with contextlib.redirect_stdout(io.StringIO()):
                timing.compare(port, ref, 'boot', ['diagnostic'])
                with self.assertRaises(ValueError):
                    timing.compare(port, ref, 'boot', ['diagnostic'], strict=True)
            rows = [json.loads(line) for line in port.read_text().splitlines()]
            rows[-1]['costs']['cd'] += 1
            port.write_text('\n'.join(map(json.dumps, rows)))
            with self.assertRaisesRegex(ValueError, 'sum'):
                timing.read(port)

    def test_shared_map_checkpoints_are_gated(self):
        self.assertTrue({'runLandingScreen', 'MOV_playMovie', 'runMainMenu',
                         'unloadNewGameScene', 'setupNameSelectorBox#2', 'loadMap',
                         'checkArenaMap'} <= timing.GATES)
        self.assertNotIn('randomLimit#17', timing.GATES)
        self.assertNotIn('gameLoop', timing.GATES)

    def test_wall_boundaries(self):
        def record(ns, resyncs=0):
            return f'wall vblanks=36000 nanoseconds={ns} resynchronizations={resyncs} rate_num=60 rate_den=1'
        with contextlib.redirect_stdout(io.StringIO()):
            wall.verify(record(600_000_000_000), 60, 1, 36000)
            for text in (record(600_016_666_667), record(600_000_000_000, 1),
                         record(599_999_999_999), 'no measurement',
                         record(720_000_000_000).replace('rate_num=60', 'rate_num=50'),
                         record(600_016_666_667).replace('36000', '36001')):
                with self.assertRaises(ValueError):
                    wall.verify(text, 60, 1, 36000)


if __name__ == '__main__':
    unittest.main()

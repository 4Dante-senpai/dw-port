#!/usr/bin/env python3
"""L1: fake processes exercise each detector without copyrighted data."""
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('runner', HERE / 'run.py')
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)
bot = runner.bot
H = 'a' * 64
HOST = Path(sys.argv[1]) if len(sys.argv) > 1 else None
RUNTIME = Path(sys.argv[2]) if len(sys.argv) > 2 else None
del sys.argv[1:]


class PlaytestTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.inputs = self.root / 'input.txt'
        self.inputs.write_text('0 0000\n10 4000\n')
        self.card = self.root / 'card.mcd'
        self.card.write_bytes(b'pristine')

    def fake(self, body):
        path = self.root / 'fake'
        path.write_text('#!' + sys.executable + '\nimport sys, time, signal, os\nfrom pathlib import Path\n'
                        'args = sys.argv\ndigest = Path(args[args.index("--digest")+1])\n'
                        + body)
        path.chmod(0o700)
        return path

    def execute(self, body, **kw):
        return runner.execute(self.fake(body), self.root / 'pack', self.inputs,
                              self.root / 'run', 120, hang_seconds=2, **kw)

    def test_clean_frozen_and_off_are_warnings(self):
        result = self.execute(f'digest.write_text("0 2 {H} {H}\\n120 2 {H} {H}\\n")\n', frozen_vblanks=60)
        self.assertEqual(result['status'], 'ok')
        self.assertEqual(result['overlays'], [2])
        self.assertIn('frozen display', result['warnings'])
        result = self.execute(f'digest.write_text("120 0 off {H}\\n")\n')
        self.assertEqual(result['status'], 'ok')
        self.assertIn('display disabled', result['warnings'])

    def test_crash_signal_assert_sanitizer_and_usage(self):
        for body, expected in [
                ('sys.exit(3)\n', 'crash'),
                ('os.kill(os.getpid(), signal.SIGTERM)\n', 'crash'),
                ('print("PORT_ASSERT file.c:42", file=sys.stderr)\nsys.exit(1)\n', 'assert'),
                ('print("ERROR: AddressSanitizer: bad", file=sys.stderr)\nsys.exit(1)\n', 'sanitizer'),
                ('print("file.c:42: runtime error: overflow", file=sys.stderr)\nsys.exit(1)\n', 'sanitizer'),
                ('sys.exit(2)\n', 'usage')]:
            with self.subTest(expected=expected, body=body):
                result = self.execute(body)
                self.assertEqual(result['status'], expected)
                self.assertTrue(Path(result['reproduction']).is_file())
                repeated = subprocess.run([result['reproduction']], capture_output=True)
                self.assertEqual(repeated.returncode, result['exit_code'])
                self.assertEqual((self.root / 'run/inputs.txt').read_bytes(), self.inputs.read_bytes())

    def test_hang_ignores_rewritten_identical_vblank(self):
        result = self.execute(f'digest.write_text("60 0 {H} {H}\\n")\nwhile True:\n time.sleep(.02)\n with digest.open("a") as f: f.write("60 0 {H} {H}\\n")\n')
        self.assertEqual(result['status'], 'hang')
        self.assertEqual(result['vblanks'], 60)

    def test_success_without_final_is_failure(self):
        self.assertEqual(self.execute('pass\n')['status'], 'crash')

    def test_card_copy_and_reproduction_restore(self):
        body = f'Path(args[args.index("--card1")+1]).write_bytes(b"changed")\ndigest.write_text("120 0 {H} {H}\\n")\n'
        self.assertEqual(self.execute(body, card=self.card)['status'], 'ok')
        self.assertEqual(self.card.read_bytes(), b'pristine')
        self.assertEqual((self.root / 'run/input.mcd').read_bytes(), b'pristine')
        subprocess.run([self.root / 'run/reproduce.sh'], check=True)
        self.assertEqual(self.card.read_bytes(), b'pristine')

    def test_invalid_inputs(self):
        for text in ['2 0000\n1 0001\n', '0 10000\n', '-1 0000\n', '0 0000 extra\n']:
            self.inputs.write_text(text)
            with self.assertRaises(ValueError):
                bot.read_inputs(self.inputs)

    def test_bot_determinism_and_prefix(self):
        prefix = [(0, 0), (30, 8), (40, 0)]
        first = bot.generate(42, 5000, prefix, 100)
        self.assertEqual(first, bot.generate(42, 5000, prefix, 100))
        self.assertNotEqual(first, bot.generate(43, 5000, prefix, 100))
        self.assertEqual(first[:3], prefix)
        outputs = []
        for n in range(2):
            path = self.root / f'bot-{n}.txt'
            subprocess.run([sys.executable, '-I', '-B', HERE / 'bot.py', '--seed', '42', '--vblanks', '5000', '--output', path], check=True)
            outputs.append(path.read_bytes())
        self.assertEqual(*outputs)

    def test_compare_different_digest(self):
        results = [dict(status='ok', vblanks=120, digest=H), dict(status='ok', vblanks=120, digest='b'*64)]
        self.assertFalse(runner.compare(results))
        results[1]['digest'] = H
        self.assertTrue(runner.compare(results))

    def test_mixed_sources_only_inputs_is_verbatim(self):
        binary = self.fake(f'digest.write_text("120 0 {H} {H}\\n")\n')
        out = self.root / 'mixed'
        self.assertEqual(runner.main(['--build', str(binary), '--inputs', str(self.inputs),
                                     '--session', str(self.inputs), '--prefix', str(self.inputs),
                                     '--vblanks', '120', '--output', str(out)]), 0)
        self.assertEqual((out / 'trial-0000/generated.txt').read_bytes(), self.inputs.read_bytes())
        for n in (1, 2):
            events = bot.read_inputs(out / f'trial-{n:04d}/generated.txt')
            self.assertEqual(events[:2], bot.read_inputs(self.inputs))
            self.assertTrue(any(k > 10 for k, _ in events))

    def test_nonfinite_timeout_and_stale_reports(self):
        out = self.root / 'reports'
        out.mkdir()
        for value in ('nan', 'inf', '-inf'):
            for name in ('report.json', 'summary.json'):
                (out / name).write_text('{"status":"ok"}')
            with self.assertRaises(SystemExit) as error:
                runner.main([f'--hang-seconds={value}', '--output', str(out)])
            self.assertEqual(error.exception.code, 2)
            self.assertFalse((out / 'summary.json').exists())
            self.assertFalse((out / 'report.json').exists())
        (out / 'summary.json').write_text('{"status":"ok"}')
        self.assertEqual(runner.main(['--inputs', str(self.root / 'missing'), '--output', str(out)]), 2)
        self.assertEqual(json.loads((out / 'report.json').read_text())['status'], 'usage')
        self.assertFalse((out / 'summary.json').exists())

    def test_mark_prefix_defaults_and_capture_end(self):
        for checkpoints in ('', 'checkpoints=["entry"]\n'):
            scene = self.root / 'scene.toml'
            scene.write_text('mark="entry"\ncapture_vblank=60\n' + checkpoints)
            def launch(cmd, work, inputs):
                self.assertEqual('--checkpoints' in cmd, bool(checkpoints))
                Path(cmd[cmd.index('--record-inputs') + 1]).write_text('0 0000\n')
                Path(cmd[cmd.index('--digest') + 1]).write_text(f'7734 0 {H} {H}\n')
            events, end = bot.resolve_prefix(scene, 'host', 'pack', self.root / 'prefix', launch=launch)
            self.assertEqual(end, 7734)
            self.assertEqual(events, [(0, 0)])
            self.assertTrue(all(k > end for k, _ in bot.generate(1, 8000, (), end)))
        scene.write_text('mark="entry"\ninputs_from="mark"\ncapture_vblank=60\n'
                         'inputs=[{vblank=1,buttons=[3]}]\n')
        def launch(cmd, work, inputs):
            self.assertNotIn('--checkpoints', cmd)
            self.assertEqual(inputs.read_text(), '1 0008\n')
            self.assertEqual(cmd[cmd.index('--inputs-from') + 1], 'mark')
            Path(cmd[cmd.index('--record-inputs') + 1]).write_text('100 0008\n')
            Path(cmd[cmd.index('--digest') + 1]).write_text(f'160 0 {H} {H}\n')
        self.assertEqual(bot.resolve_prefix(scene, 'host', 'pack', self.root / 'prefix', launch=launch)[1], 160)

    def test_web_smoke_uses_web_pack(self):
        import runpy
        class Captured(Exception):
            pass
        def capture(cmd):
            self.assertEqual(Path(cmd[cmd.index('--pack') + 1]), self.root / 'us.pack')
            raise Captured
        with mock.patch.object(sys, 'argv', ['run.py', str(self.root), 'playtest']), \
             mock.patch.dict(os.environ, {'DW1_DISC_DIR': str(self.root)}), \
             mock.patch('subprocess.run', side_effect=capture), self.assertRaises(Captured):
            runpy.run_path(str(runner.ROOT / 'port/web/tests/run.py'), run_name='__main__')

    def test_soak_creates_pack_and_rejects_missing_pack(self):
        import shlex
        script = (HERE / 'soak.sh').read_text().split('exec tools/port/remote_run.sh ', 1)[1]
        inner = shlex.split(script)[2]
        bindir = self.root / 'bin'
        bindir.mkdir()
        for name, body in [('cmake', ':'), ('ctest', 'test -n "$DW1_DISC_DIR"\necho pack >> order\n'
                            'if [ "$CREATE_PACK" = 1 ]; then mkdir -p build/port/native-headless; touch build/port/native-headless/us.pack; fi'),
                           ('python3', 'test -f build/port/native-headless/us.pack\necho run >> order')]:
            path = bindir / name
            path.write_text('#!/bin/sh\nset -eu\n' + body + '\n')
            path.chmod(0o700)
        env = dict(os.environ, PATH=str(bindir) + ':' + os.environ['PATH'], CREATE_PACK='0')
        result = subprocess.run(['sh', '-c', inner, 'sh', 'output'], cwd=self.root, env=env, capture_output=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b'us.pack missing', result.stderr)
        (self.root / 'order').unlink()
        result = subprocess.run(['sh', '-c', inner, 'sh', 'output'], cwd=self.root,
                                env=dict(env, CREATE_PACK='1'), capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual((self.root / 'order').read_text(), 'pack\nrun\n')

    def test_soak_jobs_reports_and_build_mismatch(self):
        import json
        binary = self.fake(f'digest.write_text("120 2 {H} {H}\\n")\n')
        out = self.root / 'soak'
        command = [sys.executable, '-I', '-B', HERE / 'run.py', '--soak', '--jobs', '2',
                   '--seed', '7', '--seed', '8', '--inputs', self.inputs, '--vblanks', '120',
                   '--build', binary, '--output', out]
        result = subprocess.run(command, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        report = json.loads((out / 'report.json').read_text())
        public = json.loads((out / 'summary.json').read_text())
        self.assertEqual(len(report['trials']), 2)
        self.assertAlmostEqual(report['virtual_hours'], 240 / runner.RATE / 3600)
        self.assertNotIn(str(self.root), json.dumps(public))
        other = self.root / 'other'
        other.write_bytes(binary.read_bytes().replace(H.encode(), b'b' * 64))
        other.chmod(0o700)
        result = subprocess.run(command + ['--build', other], capture_output=True)
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertIn(b'digest mismatch', result.stdout)
        mismatch = json.loads((out / 'report.json').read_text())
        self.assertEqual(mismatch['failures'], 2)
        for trial in mismatch['trials']:
            for run in trial['runs']:
                self.assertTrue(Path(run['reproduction']).is_file())

    @unittest.skipUnless(HOST, 'native host not supplied')
    def test_host_invalid_inputs_and_flags(self):
        for text in ['0 0000\n0 0001\n', '90000 10000\n']:
            self.inputs.write_text(text)
            result = subprocess.run([HOST, '--headless', '--inputs', self.inputs], capture_output=True)
            self.assertEqual(result.returncode, 2, result.stderr)
        for value in ['0', '-1', '4294967296', '1x']:
            result = subprocess.run([HOST, '--headless', '--digest-every', value], capture_output=True)
            self.assertEqual(result.returncode, 2, result.stderr)

    @unittest.skipUnless(RUNTIME, 'native runtime probe not supplied')
    def test_live_record_roundtrip_and_purity(self):
        record = self.root / 'record.txt'
        for name, mode, digest, observation in [('live', 'live', 'live.digest', 'on'),
                                               ('replay', 'replay', 'replay.digest', 'on'),
                                               ('plain', 'live', '-', 'off')]:
            result = subprocess.run([RUNTIME, mode, self.root / digest if digest != '-' else '-',
                                     self.root / (name + '.clock'), record, observation], capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(record.read_text(), '0 0008\n2 4000\n5 0000\n')
        self.assertEqual((self.root / 'live.digest').read_bytes(), (self.root / 'replay.digest').read_bytes())
        clocks = [(self.root / (name + '.clock')).read_bytes() for name in ('live', 'replay', 'plain')]
        self.assertEqual(clocks[0], clocks[1])
        self.assertEqual(clocks[0], clocks[2])

    def test_prefix_crash_uses_game_detector(self):
        prefix = self.root / 'scene.toml'
        prefix.write_text('mark="entry"\ninputs_from="mark"\ncapture_vblank=10\ncheckpoints=["entry"]\ninputs=[{after="entry",vblank=1,buttons=[3]}]\n')
        binary = self.fake('print("PORT_ASSERT prefix.c:7", file=sys.stderr)\nsys.exit(1)\n')
        out = self.root / 'prefix-run'
        result = subprocess.run([sys.executable, '-I', '-B', HERE / 'run.py', '--build', binary,
                                 '--prefix', prefix, '--output', out], capture_output=True)
        self.assertEqual(result.returncode, 1, result.stderr)
        import json
        report = json.loads((out / 'report.json').read_text())
        self.assertEqual(report['trials'][0]['runs'][0]['status'], 'assert')

    def test_no_disc(self):
        env = dict(os.environ)
        env.pop('DW1_DISC_DIR', None)
        result = subprocess.run([sys.executable, '-I', '-B', HERE / 'run.py', '--smoke'], env=env, capture_output=True)
        self.assertEqual(result.returncode, 77)


if __name__ == '__main__':
    unittest.main()

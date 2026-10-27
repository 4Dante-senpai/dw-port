#!/usr/bin/env python3
"""Exercise deployment transitions with an isolated Docker API command double."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
FAKE = r'''#!/usr/bin/env python3
import json, os, sys
from pathlib import Path
p = Path(os.environ['DOCKER_TEST_STATE'])
s = json.loads(p.read_text())
a = sys.argv[1:]
s['calls'].append(a)
code = 0
if a[:2] == ['image', 'inspect']:
    code = 0 if a[-1] in s['images'] else 1
    if code == 0 and '--format' in a: print(s['images'][a[-1]])
elif a[0] == 'build':
    tag = a[a.index('--tag') + 1]
    s['images'][tag] = 'sha256:' + tag.split(':')[1]
elif a[0] == 'create': print('helper')
elif a[:2] == ['container', 'inspect']: code = 0 if s.get('running') else 1
elif a[0] == 'tag':
    if a[1] not in s['images']: code = 1
    else: s['images'][a[2]] = s['images'][a[1]]
elif a[0] == 'run':
    if '--rm' in a: print(s.get('pack_sha', ''))
    elif s.get('fail_run') and a[-1].endswith('d'*40): code = 1
    elif a[-1] not in s['images']: code = 1
    else: s['running'] = s['images'][a[-1]]; print('container')
elif a[0] == 'images':
    print('\n'.join(tag.split(':')[1] for tag in s['images']))
elif a[0] == 'rmi':
    del s['images'][a[1]]
elif a[0] == 'cp' and a[1].endswith('.sha256'):
    s['pack_sha'] = Path(a[1]).read_text().strip()
elif a[0] == 'inspect':
    if 'ExitCode' in a[2]: print('0')
    else: print('unhealthy' if s.get('fail_new') and s.get('running', '').endswith('a'*40) else 'healthy')
p.write_text(json.dumps(s))
sys.exit(code)
'''


class DeployTest(unittest.TestCase):
    def test_initial_redeploy_failure_and_manual_rollback(self):
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)
            docker = directory / 'docker'
            docker.write_text(FAKE)
            docker.chmod(0o755)
            state = directory / 'state.json'
            state.write_text(json.dumps({'images': {}, 'calls': []}))
            build = directory / 'build'
            build.mkdir()
            for name in ('dw1.html', 'dw1.js', 'dw1.wasm', 'io_worker.js', 'dw1_pack.js', 'dw1_pack.wasm', 'us.tsv', 'seed.html', 'seed_worker.js'):
                (build / name).write_text('synthetic web asset')
            (build / 'us.pack').write_bytes(b'DW1PACK\0synthetic test only')
            env = dict(os.environ, PATH=str(directory) + ':' + os.environ['PATH'],
                       DOCKER_TEST_STATE=str(state), HUB_DOCKER_HOST='tcp://hidden.invalid:2375',
                       CI_COMMIT_SHA='b'*40)

            def run(*args, success=True):
                result = subprocess.run(['bash', str(ROOT / 'tools/port/testing/deploy.sh'), *args],
                                        env=env, text=True, capture_output=True)
                self.assertEqual(result.returncode == 0, success, result.stdout + result.stderr)
                self.assertNotIn('hidden.invalid', result.stdout + result.stderr)
                self.assertNotIn('DW1PACK', result.stdout + result.stderr)
                return result.stdout

            run('--rollback', success=False)
            run(str(build))
            first = json.loads(state.read_text())
            first_id = first['images']['dw1-testing:current']
            self.assertEqual(first['running'], first_id)
            self.assertEqual(sum(c[0] == 'cp' for c in first['calls']), 2)
            env['CI_COMMIT_SHA'] = 'c'*40
            self.assertIn('copy skipped', run(str(build)))
            second = json.loads(state.read_text())
            self.assertEqual(sum(c[0] == 'cp' for c in second['calls']), 2)
            second_id = second['images']['dw1-testing:current']
            self.assertNotEqual(first_id, second_id)
            self.assertEqual(second['running'], second_id)
            self.assertEqual(second['images']['dw1-testing:previous'], first_id)
            # Retrying the same commit must preserve the real rollback image.
            run(str(build))
            retry = json.loads(state.read_text())
            self.assertEqual(retry['running'], second_id)
            self.assertEqual(retry['images']['dw1-testing:previous'], first_id)
            run('--rollback')
            restored = json.loads(state.read_text())
            self.assertEqual(restored['running'], first_id)
            self.assertEqual(restored['images']['dw1-testing:current'], first_id)
            run(str(build))
            second = json.loads(state.read_text())
            self.assertIn('dw1-testing-pack:/pack:ro', next(c for c in second['calls'] if c[0:2] == ['run', '-d']))
            second['fail_new'] = True
            state.write_text(json.dumps(second))
            env['CI_COMMIT_SHA'] = 'a'*40
            self.assertIn('restored', run(str(build), success=False))
            self.assertEqual(json.loads(state.read_text())['running'], second_id)
            run('--rollback')
            final = json.loads(state.read_text())
            self.assertEqual(final['running'], second_id)
            final['fail_run'] = True
            state.write_text(json.dumps(final))
            env['CI_COMMIT_SHA'] = 'd'*40
            self.assertIn('restored', run(str(build), success=False))
            self.assertEqual(json.loads(state.read_text())['running'], second_id)


if __name__ == '__main__':
    unittest.main()

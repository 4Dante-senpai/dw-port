#!/usr/bin/env python3
"""Start an ephemeral dev server and run the pinned browser suite inside dw-port."""
import socket
import json
import hashlib
import struct
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import functools
import importlib.util

root = Path(__file__).resolve().parents[3]
# Accept both the established <build> <suite> form and the documented suite shortcut.
if len(sys.argv) == 2 and sys.argv[1] == 'playtest':
    sys.argv[1:] = [str(root / 'build/port/web'), 'playtest']
build = Path(sys.argv[1]).resolve()
suite = sys.argv[2] if len(sys.argv) > 2 else 'boot'
if suite in ('title', 'title_audio', 'disc_real', 'cards_real', 'cards_redux', 'offline_real', 'long', 'playtest') and not (os.environ.get('DW1_DISC_DIR') or (suite == 'title' and os.environ.get('DW1_WEB_URL'))):
    print(f'web_{"scene_title" if suite == "title" else suite}: DW1_DISC_DIR not set; skipped')
    sys.exit(77)
if suite in ('io', 'launcher', 'shell', 'cards'):
    # Same sector/byte formula as pack/tests/test_pack.c; no disc bytes.
    data = bytes(((22 + i // 2048) * 31 + (24 + i % 2048) * 7) & 255 for i in range(5000))
    header = struct.pack('<8sII32sII8x', b'DW1PACK\0', 1, 1, bytes(32), 64, 2048)
    entry = struct.pack('<64sIIIII32s12x', b'\\SLUS_010.32;1', 22, 3, 1, len(data), 2048,
                        hashlib.sha256(data).digest())
    pack = header + entry + bytes(2048 - len(header) - len(entry)) + data
    (build / 'synthetic.pack').write_bytes(pack)
    (build / 'corrupt.pack').write_bytes(bytes(len(pack)))
    # Correct Manifest, damaged canonical entry => exercises the shared C index validator.
    corrupt = bytearray(pack)
    corrupt[16:48] = hashlib.sha256((root / 'tools/port/manifest/us.tsv').read_bytes()).digest()
    corrupt[64 + 80] ^= 1
    (build / 'bad-index.pack').write_bytes(corrupt)

if suite == 'playtest':
    # Same seed and frame0 prefix as native smoke; no external card fixture.
    result = subprocess.run([sys.executable, '-I', '-B', str(root / 'tools/port/playtest/run.py'),
        '--smoke', '--build', 'native-headless', '--pack', str(build / 'us.pack'),
        '--output', str(build / 'playtest-native')])
    if result.returncode: sys.exit(result.returncode)
    trial = build / 'playtest-native/trial-0000'
    (build / 'playtest.replay.txt').write_bytes((trial / 'generated.txt').read_bytes())
    report = json.loads((build / 'playtest-native/report.json').read_text())
    os.environ['DW1_PLAYTEST_VBLANKS'] = str(report['trials'][0]['runs'][0]['vblanks'])
    os.environ['DW1_PLAYTEST_DIGEST'] = report['trials'][0]['runs'][0]['digest']

if suite == 'cards_redux':
    prepared = subprocess.run([sys.executable, '-I', '-B', str(root / 'tools/port/scenes/cards_redux.py'), 'prepare'])
    if prepared.returncode: sys.exit(prepared.returncode)
env = dict(os.environ, DW1_WEB_SUITE=suite, DW1_WEB_BUILD=str(build))
server = None
if suite == 'long' and not (build / 'us.pack').is_file():
    print('web_long: us.pack missing; skipped')
    sys.exit(77)
if suite in ('title', 'title_audio', 'disc_real', 'cards_real', 'cards_redux', 'offline_real', 'long', 'playtest') and not env.get('DW1_WEB_URL'):
    with (build / 'us.pack').open('rb') as pack_file:
        (build / 'us.pack.sha256').write_text(hashlib.file_digest(pack_file, 'sha256').hexdigest() + '\n')
if suite.startswith('offline'):
    with socket.socket() as available:
        available.bind(('127.0.0.1', 0))
        env['DW1_WEB_URL'] = f'http://127.0.0.1:{available.getsockname()[1]}'
if not env.get('DW1_WEB_URL'):
    spec = importlib.util.spec_from_file_location('serve', root / 'port/web/dev/serve.py')
    serve = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(serve)
    server = serve.http.server.ThreadingHTTPServer(('127.0.0.1', 0),
        functools.partial(serve.Handler, directory=str(build)))
    threading.Thread(target=server.serve_forever, daemon=True).start()
    env['DW1_WEB_URL'] = f'http://127.0.0.1:{server.server_port}'
if suite == 'title':
    native = root / 'build/port/native-headless/scenes/title/run-1.ppm'
    env['DW1_NATIVE_TITLE_SHA'] = hashlib.sha256(native.read_bytes()).hexdigest()
cli = Path('/opt/node_modules/@playwright/test/cli.js')
version = json.loads(cli.with_name('package.json').read_text())['version']
if version != '1.64.0':
    sys.exit(f'Playwright 1.64.0 required, found {version}')
# pulseaudio --kill does not wait, so its socket can still be there at cleanup.
pulse = tempfile.TemporaryDirectory(prefix='dw1-pulse-', ignore_cleanup_errors=True)
env['XDG_RUNTIME_DIR'] = pulse.name
try:
    # Firefox keeps every AudioContext suspended without an audio server.
    subprocess.run(['pulseaudio', '-n', '--daemonize=yes', '--exit-idle-time=-1',
        '--load=module-null-sink sink_name=dw1', '--load=module-native-protocol-unix'], env=env, check=True)
    result = subprocess.run(['xvfb-run', '-a', 'node', str(cli), 'test', '--config',
        str(Path(__file__).with_name('playwright.config.cjs')), *sys.argv[3:]], env=env, cwd=build)
    if result.returncode == 0 and suite == 'boot':
        timings = {browser: {kind: json.loads((build / f'timing-{browser}-{kind}.json').read_text())
                   for kind in ('game', 'pattern')} for browser in ('chromium', 'firefox')}
        (build / 'present-timing.json').write_text(json.dumps(timings, indent=2) + '\n')
    sys.exit(result.returncode)
finally:
    subprocess.run(['pulseaudio', '--kill'], env=env)
    pulse.cleanup()
    if server:
        server.shutdown()
        server.server_close()

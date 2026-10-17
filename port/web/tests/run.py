#!/usr/bin/env python3
"""Start an ephemeral dev server and run the pinned browser suite inside dw-port."""
import json
import os
from pathlib import Path
import subprocess
import sys
import threading
import functools
import importlib.util

root = Path(__file__).resolve().parents[3]
build = Path(sys.argv[1]).resolve()
spec = importlib.util.spec_from_file_location('serve', root / 'port/web/dev/serve.py')
serve = importlib.util.module_from_spec(spec)
spec.loader.exec_module(serve)
server = serve.http.server.ThreadingHTTPServer(('127.0.0.1', 0),
    functools.partial(serve.Handler, directory=str(build)))
thread = threading.Thread(target=server.serve_forever, daemon=True)
thread.start()
env = dict(os.environ, DW1_WEB_BUILD=str(build),
           DW1_WEB_URL=f'http://127.0.0.1:{server.server_port}')
cli = Path('/opt/node_modules/@playwright/test/cli.js')
version = json.loads(cli.with_name('package.json').read_text())['version']
if version != '1.64.0':
    sys.exit(f'Playwright 1.64.0 required, found {version}')
try:
    result = subprocess.run(['xvfb-run', '-a', 'node', str(cli), 'test', '--config',
        str(Path(__file__).with_name('playwright.config.cjs'))], env=env, cwd=build)
    if result.returncode == 0:
        timings = {browser: {kind: json.loads((build / f'timing-{browser}-{kind}.json').read_text())
                   for kind in ('game', 'pattern')} for browser in ('chromium', 'firefox')}
        (build / 'present-timing.json').write_text(json.dumps(timings, indent=2) + '\n')
    sys.exit(result.returncode)
finally:
    server.shutdown()
    server.server_close()

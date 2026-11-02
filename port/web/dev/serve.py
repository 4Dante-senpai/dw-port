#!/usr/bin/env python3
"""Local development server; /isolated-off/ intentionally omits COOP/COEP for L3a."""
import argparse
import functools
import http.server
import os
import time
from pathlib import Path
from urllib.parse import urlsplit

class Handler(http.server.SimpleHTTPRequestHandler):
    def do_GET(self):
        # Deterministic install faults for offline tests; disabled for normal dev hosts.
        if os.environ.get('DW1_OFFLINE_TEST_SERVER') == '1':
            directory = Path(self.directory)
            failure = directory / 'fail'
            if failure.exists() and self.path == failure.read_text():
                (directory / 'fail-hit').touch()
                self.send_error(503)
                return
            gate = 'metadata-gate' if self.path == '/version.txt' else 'gate' if self.path == '/dw1.wasm' else None
            while gate and (directory / gate).exists():
                time.sleep(0.02)
        super().do_GET()

    def end_headers(self):
        if not self.path.startswith('/isolated-off/'):
            self.send_header('Cross-Origin-Opener-Policy', 'same-origin')
            self.send_header('Cross-Origin-Embedder-Policy', 'require-corp')
        self.send_header('Cache-Control', 'no-store')
        super().end_headers()

    def translate_path(self, path):
        requested = urlsplit(path).path
        if requested.startswith('/__fixture/'):
            name = requested.removeprefix('/__fixture/')
            if name in ('card_newgame.mcd', 'card_continue.mcd', 'card_newgame.replay.txt',
                        'card_continue.replay.txt', 'cards-fixture.json'):
                return str(Path(os.environ.get('DW1_CARD_DIR_WEB', '/tmp/dw1-web-cards')) /
                           ('fixture.json' if name == 'cards-fixture.json' else name))
            if name in ('replay.txt', 'fixture.json'):
                return str(Path(os.environ.get('DW1_PCM_DIR', '/tmp/dw1-web-title-audio')) / name)
            if name in ('synthetic.pack', 'corrupt.pack', 'bad-index.pack', 'us.pack', 'us.pack.sha256'):
                return str(Path(self.directory) / name)
        if urlsplit(path).path in ('/', '/index.html', '/isolated-off/'):
            return str(Path(self.directory) / 'dw1.html')
        return super().translate_path(path)

    def log_message(self, *args):
        pass

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--build', required=True)
    parser.add_argument('--port', type=int, default=8765)
    args = parser.parse_args()
    # addAll fetches the program in parallel; avoid TCP retries at the default backlog of five.
    http.server.ThreadingHTTPServer.request_queue_size = 64
    server = http.server.ThreadingHTTPServer(('127.0.0.1', args.port),
        functools.partial(Handler, directory=args.build))
    server.serve_forever()

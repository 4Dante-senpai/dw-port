#!/usr/bin/env python3
"""Local development server; /isolated-off/ intentionally omits COOP/COEP for L3a."""
import argparse
import functools
import http.server
from pathlib import Path
from urllib.parse import urlsplit

class Handler(http.server.SimpleHTTPRequestHandler):
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
    server = http.server.ThreadingHTTPServer(('127.0.0.1', args.port),
        functools.partial(Handler, directory=args.build))
    server.serve_forever()

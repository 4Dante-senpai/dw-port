#!/usr/bin/env python3
"""Stamp the service worker with the content hash of the complete program."""
import hashlib
import re
import sys
from pathlib import Path


def stamp(directory):
    worker = directory / 'sw.js'
    source = worker.read_text()
    normalized, count = re.subn(r"const VERSION = '[^']*';", "const VERSION = '';", source)
    if count != 1:
        raise ValueError('sw.js must declare exactly one VERSION')
    files = re.search(r'const FILES = \[(.*?)\];', source, re.S)
    if not files:
        raise ValueError('sw.js must declare FILES')
    names = re.findall(r"'([^']+)'", files[1])
    digest = hashlib.sha256(normalized.encode())
    for name in names + (['/version.txt'] if (directory / 'version.txt').exists() else []):
        digest.update(name.encode() + b'\0')
        path = directory / ('dw1.html' if name == '/' else name.lstrip('/'))
        digest.update(hashlib.sha256(path.read_bytes()).digest())
    version = digest.hexdigest()
    worker.write_text(normalized.replace("const VERSION = '';", f"const VERSION = '{version}';"))
    return version


if __name__ == '__main__':
    print(stamp(Path(sys.argv[1])))

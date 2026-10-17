#!/usr/bin/env python3
"""Reject Pack headers and complete Manifest files without printing their bytes."""
import hashlib
from pathlib import Path
import sys


def check(context, manifest):
    hashes = {line.split('\t')[-1] for line in manifest.read_text().splitlines()
              if line and not line.startswith('#')}
    for path in context.rglob('*'):
        if path.is_symlink():
            raise ValueError('build context contains a symlink')
        if not path.is_file():
            continue
        digest = hashlib.sha256()
        with path.open('rb') as stream:
            if stream.read(7) == b'DW1PACK':
                raise ValueError('build context contains a Pack header')
            stream.seek(0)
            for chunk in iter(lambda: stream.read(1024 * 1024), b''):
                digest.update(chunk)
        if digest.hexdigest() in hashes:
            raise ValueError('build context contains a Manifest file')


if __name__ == '__main__':
    try:
        check(Path(sys.argv[1]), Path(sys.argv[2]))
    except ValueError as error:
        sys.exit(str(error))
    print('build context: clean')

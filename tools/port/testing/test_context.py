#!/usr/bin/env python3
import hashlib
from pathlib import Path
import tempfile
import unittest
from check_context import check


class ContextTest(unittest.TestCase):
    def test_clean_header_and_manifest_bytes(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            context = root / 'context'
            context.mkdir()
            manifest = root / 'us.tsv'
            payload = b'private synthetic fixture'
            manifest.write_text('# test\n' + '\t'.join(['x'] * 5 + [hashlib.sha256(payload).hexdigest()]) + '\n')
            file = context / 'asset'
            file.write_bytes(b'public web program')
            check(context, manifest)
            file.write_bytes(b'DW1PACK\0synthetic')
            with self.assertRaisesRegex(ValueError, 'Pack header'):
                check(context, manifest)
            file.write_bytes(payload)
            with self.assertRaisesRegex(ValueError, 'Manifest file'):
                check(context, manifest)
            file.unlink()
            file.symlink_to(manifest)
            with self.assertRaisesRegex(ValueError, 'symlink'):
                check(context, manifest)


if __name__ == '__main__':
    unittest.main()

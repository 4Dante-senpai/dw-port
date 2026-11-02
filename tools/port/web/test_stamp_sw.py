#!/usr/bin/env python3
"""Content stamping includes every served asset and optional deploy metadata."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location('stamp_sw', Path(__file__).with_name('stamp_sw.py'))
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class StampTest(unittest.TestCase):
    def test_content_and_idempotence(self):
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)
            source = (ROOT / 'port/web/sw.js').read_text()
            worker = directory / 'sw.js'
            worker.write_text(source)
            files = ['dw1.html', 'dw1.js', 'dw1.wasm', 'io_worker.js', 'audio_worklet.js',
                     'dw1_pack.js', 'dw1_pack.wasm', 'us.tsv']
            for name in files:
                (directory / name).write_bytes(b'asset')
            baseline = module.stamp(directory)
            stamped = worker.read_bytes()
            self.assertEqual(module.stamp(directory), baseline)
            self.assertEqual(worker.read_bytes(), stamped)
            for name in files:
                (directory / name).write_bytes(b'changed')
                self.assertNotEqual(module.stamp(directory), baseline, name)
                (directory / name).write_bytes(b'asset')
                self.assertEqual(module.stamp(directory), baseline)
            (directory / 'version.txt').write_text('deploy-commit\n')
            self.assertNotEqual(module.stamp(directory), baseline)
            (directory / 'version.txt').unlink()
            self.assertEqual(module.stamp(directory), baseline)
            worker.write_text(source + '\n// Program policy changed.\n')
            self.assertNotEqual(module.stamp(directory), baseline)


if __name__ == '__main__':
    unittest.main()

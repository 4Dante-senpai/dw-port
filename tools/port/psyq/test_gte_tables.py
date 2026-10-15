#!/usr/bin/env python3
"""Synthetic config tests: segment relocation, symbol order, aliases and validation."""
import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import gte_tables


class Tables(unittest.TestCase):
    def test_regions(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            (root / "main.yaml").write_text("segments:\n - {name: header, start: 0, vram: 0}\n"
                                            " - {name: main, start: 0x80, vram: 0x1000}\n")
            (root / "symbols.txt").write_text("ratan_tbl = 0x1800;\nend = 0x1900;\n"
                "rsin_tbl = 0x1100;\nalias = 0x1100;\nSQRT = 0x1200;\n"
                "state = 0x1280;\nrcossin_tbl = 0x1400; // comment\n")
            self.assertEqual(gte_tables.values(root), {
                "rsin_tbl": (0x180, 0x100), "SQRT": (0x280, 0x80),
                "rcossin_tbl": (0x480, 0x400), "ratan_tbl": (0x880, 0x100)})
            out = root / "generated/tables.h"
            gte_tables.generate(root, out)
            self.assertIn("#define DW1_GTE_SQRT_SIZE 0x80u", out.read_text())
            before = out.stat().st_mtime_ns
            gte_tables.generate(root, out)
            self.assertEqual(before, out.stat().st_mtime_ns)
            (root / "symbols.txt").write_text((root / "symbols.txt").read_text().replace("0x1280", "0x1281"))
            with self.assertRaises(ValueError):
                gte_tables.values(root)
            (root / "symbols.txt").write_text("rsin_tbl = 0x1100;\n")
            with self.assertRaises((KeyError, StopIteration)):
                gte_tables.values(root)


if __name__ == "__main__":
    unittest.main()

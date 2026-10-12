#!/usr/bin/env python3
"""Tests for rts_info.py. Run from the repo root."""
import os
import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rts_info  # noqa: E402


class RtsInfo(unittest.TestCase):
    def test_us(self):
        # Same words as in SLUS_010.32 at rom 0x83A9C/0x83AA4/0x83AB4 (checked in story 2.6).
        self.assertEqual(rts_info.values(pathlib.Path("config/us")),
                         {"_stack_addr": 0x801FFF00, "_stack_size": 0x40, "_end": 0x801BFB64})

    def test_malformed(self):
        d = pathlib.Path(tempfile.mkdtemp())
        (d / "main.ld").write_text("_stack_addr = .;\nLONG(0x801FFF00)\n_end = .;\nLONG(main_BSS_END)\n")
        (d / "bss.yaml").write_text("end: 0x801BFB64\n")
        with self.assertRaises(SystemExit):
            rts_info.values(d)  # no _stack_size


if __name__ == "__main__":
    unittest.main()

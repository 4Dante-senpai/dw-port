#!/usr/bin/env python3
"""Tests for ovl_starts.py. Run from the repo root."""
import os
import pathlib
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ovl_starts  # noqa: E402


class OverlayStarts(unittest.TestCase):
    def test_us(self):
        found, errors = ovl_starts.starts(pathlib.Path("config/us/overlay.ld").read_text())
        self.assertEqual(errors, [])
        self.assertEqual(len(found), 15)
        # USA: SHOP has no segment of its own; utils2.c defines shop_START by hand.
        self.assertNotIn("shop", found)
        self.assertEqual(found["btl"], 0x80052AE0)  # btl.yaml: texture vram 0x80052AE0
        self.assertEqual(found["dget"], 0x80080800)  # same address as the hand-made shop_START

    def test_render_is_guarded(self):
        text = ovl_starts.render({"btl": 0x80052AE0})
        self.assertIn("#ifndef btl_START\n#define btl_START ((char *)PS1_RAM_ADDR(0x80052AE0u))",
                      text)

    def test_errors(self):
        _, errors = ovl_starts.starts(
            "OVERLAY BASE : NOCROSSREFS\n{\na_START = .;\n"
            "OVERLAY 0x90000000 : NOCROSSREFS\n{\nb_START = .;\nb_START = .;\n")
        self.assertEqual(len(errors), 4, errors)  # unparsed, a outside, range, duplicate b


if __name__ == "__main__":
    unittest.main()

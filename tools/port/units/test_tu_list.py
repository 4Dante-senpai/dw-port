#!/usr/bin/env python3
"""Tests for tu_list.py. Run from the repo root: python3 -I tools/port/units/test_tu_list.py"""
import os
import pathlib
import re
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tu_list  # noqa: E402


class RealConfig(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        rows, cls.errors = tu_list.tu_list(".", "us")
        cls.unit = {path: unit for path, unit, _ in rows}
        cls.status = {path: status for path, _, status in rows}

    def test_no_errors(self):
        self.assertEqual(self.errors, [])

    def test_count(self):
        self.assertEqual(len(self.unit), 120)

    def test_cross_unit_files_go_to_main(self):
        for f in ("src/vs/vs_intro.c", "src/vs/vs_combat.c", "src/vs/vs_select.c",
                  "src/shop/script_menu.c"):
            self.assertEqual(self.unit[f], "main", f)

    def test_overlay_files(self):
        self.assertEqual(self.unit["src/vs/vs_main.c"], "vs")
        self.assertEqual(self.unit["src/mov/mov_bss_us.c"], "mov")

    def test_no_other_versions(self):
        bad = [f for f in self.unit if re.search(r"_(eu|jp\w*)\.c$", f)]
        self.assertEqual(bad, [])

    def test_crt_replaced(self):
        self.assertEqual(self.status["src/main/_psstart.c"], "replaced")
        self.assertEqual(list(self.status.values()).count("replaced"), 1)


class SyntheticConfig(unittest.TestCase):
    def make_tree(self, mk, yamls):
        root = tempfile.mkdtemp()
        os.makedirs(os.path.join(root, "mk", "version"))
        os.makedirs(os.path.join(root, "config", "xx"))
        pathlib.Path(root, "mk", "version", "xx.mk").write_text(mk)
        for name, text in yamls.items():
            pathlib.Path(root, "config", "xx", name).write_text(text)
        return root

    SEG = "name: {n}\noptions:\n  src_path: src/{n}\nsegments:\n  - name: {n}\n    subsegments:\n{subs}"

    def test_relative_path_and_continuation(self):
        root = self.make_tree(
            "MAIN_C_SRC := \\\n\tsrc/main/a.c\nMAIN_C_SRC += src/vs/b.c\n",
            {"main.yaml": self.SEG.format(n="main", subs="      - [0x0, c, a]\n      - [0x10, c, ../vs/b]\n")})
        rows, errors = tu_list.tu_list(root, "xx")
        self.assertEqual(errors, [])
        self.assertEqual(rows, [("src/main/a.c", "main", "build"), ("src/vs/b.c", "main", "build")])

    def test_unreferenced_file_fails(self):
        root = self.make_tree("MAIN_C_SRC := src/main/a.c src/main/orphan.c\n",
                              {"main.yaml": self.SEG.format(n="main", subs="      - [0x0, c, a]\n")})
        _, errors = tu_list.tu_list(root, "xx")
        self.assertEqual(len(errors), 1)
        self.assertIn("src/main/orphan.c", errors[0])

    def test_file_in_two_segments_fails(self):
        root = self.make_tree("MAIN_C_SRC := src/main/a.c\n", {
            "main.yaml": self.SEG.format(n="main", subs="      - [0x0, c, a]\n"),
            "vs.yaml": self.SEG.format(n="vs", subs="      - [0x0, .data, ../main/a]\n")})
        _, errors = tu_list.tu_list(root, "xx")
        self.assertEqual(len(errors), 1)
        self.assertIn("['main', 'vs']", errors[0])


if __name__ == "__main__":
    unittest.main()

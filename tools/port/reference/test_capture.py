#!/usr/bin/env python3
"""Class A reference adapter checks; no BIOS or game data required."""
import contextlib
import ctypes
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location("capture", Path(__file__).with_name("capture.py"))
capture = importlib.util.module_from_spec(spec)
spec.loader.exec_module(capture)


class ConversionTests(unittest.TestCase):
    def test_rgb555(self):
        # Independently tabulated PS1 DAC expansion, including MSB ignored.
        levels = [0, 8, 16, 24, 33, 41, 49, 57, 66, 74, 82, 90, 99, 107,
                  115, 123, 132, 140, 148, 156, 165, 173, 181, 189, 198, 206,
                  214, 222, 231, 239, 247, 255]
        for shift, channel in ((0, 0), (5, 1), (10, 2)):
            for value, expected in enumerate(levels):
                rgb = [0, 0, 0]
                rgb[channel] = expected
                self.assertEqual(capture.to_ppm(struct.pack("<H", 0x8000 | value << shift),
                                                1, 1, 16), b"P6\n1 1\n255\n" + bytes(rgb))

    def test_rgb24(self):
        self.assertEqual(capture.to_ppm(bytes([1, 127, 255, 45, 16, 89]), 2, 1, 24),
                         b"P6\n2 1\n255\n" + bytes([1, 127, 255, 45, 16, 89]))

    def test_bad_display(self):
        for raw, w, h, bpp in ((b"", 1, 1, 16), (b"", 0, 1, 24), (b"", 641, 1, 16),
                               (b"", 1, 481, 16), (b"", 1, 1, 32)):
            with self.assertRaises(ValueError):
                capture.to_ppm(raw, w, h, bpp)


class PortDisplayTests(unittest.TestCase):
    def test_conversion_matches_production_display(self):
        # Compile the production display reader, independent of the Python converter.
        # Exhaust all 15-bit colors, plus the ignored high bit, in two displays.
        with tempfile.TemporaryDirectory() as directory:
            library = Path(directory) / "gpu.so"
            subprocess.run(["cc", "-shared", "-fPIC", "-O2", "-o", str(library),
                            str(capture.ROOT / "port/backend/gpu_soft/gpu_soft.c")], check=True,
                           capture_output=True)
            gpu = ctypes.CDLL(str(library))
            class Work(ctypes.Structure):
                _fields_ = [("words", ctypes.c_uint64), ("pixels", ctypes.c_uint64)]
            gpu.gpu_load.restype = Work
            gpu.gpu_reset(1)
            gpu.gpu_set_display_enable(1)
            for high in (0, 0x8000):
                words = (ctypes.c_uint16 * 32768)(*(n | high for n in range(32768)))
                gpu.gpu_load(0, 0, 256, 128, words)
                gpu.gpu_set_display(0, 0, 256, 128, 0)
                output = (ctypes.c_uint32 * 32768)()
                w, h = ctypes.c_int(), ctypes.c_int()
                self.assertEqual(gpu.gpu_read_display(output, ctypes.byref(w), ctypes.byref(h)), 1)
                expected = bytes(channel for pixel in output
                                 for channel in (pixel >> 16, (pixel >> 8) & 255, pixel & 255))
                ppm = capture.to_ppm(bytes(words), w.value, h.value, 16)
                self.assertEqual(ppm.split(b"255\n", 1)[1], expected)


class RunnerTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name) / "repo"
        self.scene = self.root / "tools/port/scenes/title.toml"
        self.scene.parent.mkdir(parents=True)
        self.original = 'mark = "titleMark"\ncapture_vblank = 60\ninputs = []\n'
        self.scene.write_text(self.original)
        symbols = self.root / "config/us/symbols.txt"
        symbols.parent.mkdir(parents=True)
        symbols.write_text('main = 0x80001000;\nendMain = 0x80001100;\n'
                           'VSync = 0x80002000;\ntitleMark = 0x80003000;\n')
        self.symbols = symbols
        lua = self.root / "tools/port/reference/capture.lua"
        lua.parent.mkdir()
        lua.write_text('-- adapter fixture')
        self.disc = Path(self.temp.name) / "disc"
        self.disc.mkdir()
        (self.disc / "fixture.cue").touch()
        self.bios = Path(self.temp.name) / "openbios.bin"
        self.bios.touch()
        self.bin = Path(self.temp.name) / "bin"
        self.bin.mkdir()
        xvfb = self.bin / "xvfb-run"
        xvfb.write_text('#!/bin/sh\nshift\nexec "$@"\n')
        xvfb.chmod(0o755)
        emulator = self.bin / "pcsx-redux"
        emulator.write_text('''#!/usr/bin/python3
import json, os, pathlib, sys, time
assert "-interpreter" in sys.argv and "-softgpu" in sys.argv and "-debugger" in sys.argv
work = pathlib.Path.cwd()
mode = os.environ.get("FAKE_MODE", "ok")
(work / "progress.txt").write_text("frame 0")
if mode == "timeout": time.sleep(30)
if mode == "fail": sys.exit(1)
pixels = bytes(range(96))
if mode == "different" and work.name == "work-2": pixels = pixels[::-1]
if mode == "black": pixels = bytes(96)
(work / "display.raw").write_bytes(pixels)
(work / "display.json").write_text(json.dumps(dict(width=32, height=1, bpp=24)))
''')
        emulator.chmod(0o755)
        self.env = patch.dict(os.environ, {"DW1_DISC_DIR": str(self.disc),
            "PATH": str(self.bin), "FAKE_MODE": "ok"}, clear=True)
        self.env.start()
        self.addCleanup(self.env.stop)
        self.bios_patch = patch.object(capture, "OPENBIOS", self.bios)
        self.bios_patch.start()
        self.addCleanup(self.bios_patch.stop)
        self.root_patch = patch.object(capture, "ROOT", self.root)
        self.root_patch.start()
        self.addCleanup(self.root_patch.stop)
        self.stdout = contextlib.redirect_stdout(io.StringIO())
        self.stdout.__enter__()
        self.addCleanup(self.stdout.__exit__, None, None, None)

    def run_capture(self, *args):
        return capture.main(["title", *args])

    def test_symbols(self):
        self.assertEqual(capture.resolve_symbols(self.symbols, "titleMark"),
                         dict(vsync=0x80002000, mark=0x80003000))
        with self.assertRaisesRegex(ValueError, "missing symbol"):
            capture.resolve_symbols(self.symbols, "missing")

    def test_no_disc(self):
        os.environ.pop("DW1_DISC_DIR")
        self.assertEqual(self.run_capture(), 77)
        os.environ["DW1_DISC_DIR"] = str(self.bin)
        self.assertEqual(self.run_capture(), 77)

    def test_no_emulator(self):
        (self.bin / "pcsx-redux").unlink()
        self.assertEqual(self.run_capture(), 77)

    def test_capture_and_update(self):
        self.assertEqual(self.run_capture("--update"), 0)
        expected = hashlib.sha256(b"P6\n32 1\n255\n" + bytes(range(96))).hexdigest()
        self.assertEqual(capture.tomllib.loads(self.scene.read_text())["reference_sha256"], expected)
        self.assertEqual(self.run_capture(), 0)
        self.assertEqual(self.run_capture("--update"), 0)
        self.assertEqual(self.scene.read_text().count("reference_sha256"), 1)

    def test_pinned_mismatch(self):
        self.scene.write_text(self.original + 'reference_sha256 = "' + '0' * 64 + '"\n')
        with self.assertRaisesRegex(ValueError, "actual=[0-9a-f]{64} expected=0{64}"):
            self.run_capture()

    def test_non_determinism_preserves_evidence(self):
        os.environ["FAKE_MODE"] = "different"
        with self.assertRaisesRegex(ValueError, "[0-9a-f]{64} != [0-9a-f]{64}"):
            self.run_capture("--update")
        self.assertEqual(self.scene.read_text(), self.original)
        self.assertTrue((self.root / "build/reference/title/run-1.ppm").exists())
        self.assertTrue((self.root / "build/reference/title/run-2.ppm").exists())

    def test_timeout_and_failure_progress(self):
        for mode in ("timeout", "fail"):
            os.environ["FAKE_MODE"] = mode
            with self.assertRaisesRegex(ValueError, "last point: frame 0; log:"):
                self.run_capture("--timeout", "0.1")
            self.assertTrue((self.root / "build/reference/title/run-1.log").is_file())

    def test_black(self):
        os.environ["FAKE_MODE"] = "black"
        with self.assertRaisesRegex(ValueError, "only 1 colors"):
            self.run_capture()

    def test_bios(self):
        self.assertEqual(self.run_capture("--bios", str(self.bios)), 0)
        self.assertEqual(self.scene.read_text(), self.original)
        with self.assertRaisesRegex(ValueError, "cannot be combined"):
            self.run_capture("--bios", str(self.bios), "--update")
        with self.assertRaisesRegex(ValueError, "existing file outside"):
            self.run_capture("--bios", str(self.bios) + "missing")
        with self.assertRaisesRegex(ValueError, "existing file outside"):
            self.run_capture("--bios", str(self.scene))

    def test_output_policy(self):
        with self.assertRaisesRegex(ValueError, "output must"):
            self.run_capture("--out", str(self.root / "assets"))
        external = Path(self.temp.name) / "private"
        os.environ["DW1_REFERENCE_DIR"] = str(external)
        self.assertEqual(self.run_capture(), 0)
        self.assertTrue((external / "title/run-1.ppm").exists())

    def test_input_validation(self):
        capture.validate_scene(dict(capture_vblank=60, inputs=[dict(vblank=0, buttons=[3]),
                                                               dict(vblank=20, buttons=[])]))
        for events in ([dict(vblank=-1, buttons=[])], [dict(vblank=0, buttons=[16])],
                       [dict(vblank=1, buttons=[]), dict(vblank=0, buttons=[])]):
            with self.assertRaises(ValueError):
                capture.validate_scene(dict(capture_vblank=60, inputs=events))


if __name__ == "__main__":
    unittest.main()

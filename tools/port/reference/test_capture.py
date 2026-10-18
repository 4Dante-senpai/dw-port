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
import shutil
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


class AdapterTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which("pcsx-redux") and shutil.which("xvfb-run"), "PCSX-Redux Lua runtime unavailable")
    def test_real_lua_checkpoint_callbacks_without_disc(self):
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            script = work / "mock.lua"
            script.write_text("local work = " + json.dumps(str(work)) + "\n" + r'''
local quit = PCSX.quit
local callbacks, vsync, buttons = {}, nil, {}
local created, removed = 0, 0
local registers = {GPR={n={ra=700,v0=123}}}
local pad = {
 clearOverride=function(b) buttons[b]=nil end,
 setOverride=function(b) buttons[b]=true end,
 getButton=function(b) return buttons[b] == true end,
}
PCSX = {
 SIO0={slots={{pads={pad}}}},
 addBreakpoint=function(address, kind, size, name, callback)
   local bp={active=true,callback=callback}
   function bp:disable() self.active=false end
   function bp:enable() self.active=true end
   callbacks[address]=bp
   return bp
 end,
 getRegisters=function() return registers end,
 getMemPtr=function() return {[0x10000]=42} end,
 Events={createEventListener=function(name,callback)
   created=created+1; vsync=callback; return {remove=function() removed=removed+1 end}
 end},
 GPU={takeScreenShot=function() return {data="abc",bpp=1,width=1,height=1} end},
 quit=function(code) assert(code==0); end,
}
DW1_CAPTURE={mark=100,vsync=200,rand=300,progress=work.."/progress",state_trace=work.."/state",
 raw=work.."/raw",meta=work.."/meta",max_vblanks=20000,capture_vblank=20,inputs_from="mark",
 checkpoints={{label="mark",address=100,occurrence=1,window=1},
              {label="loop#2",address=400,occurrence=2,window=1}},
 inputs={{after="mark",vblank=0,buttons={3}}, {after="loop#2",vblank=0,buttons={14}}},
 state_vars={{name="TEST",type="u8",address=0x80010000,count=1,width=1}},state_symbols={}}
''' + "\ndofile(" + json.dumps(str(capture.ROOT / "tools/port/reference/capture.lua")) + ")\n" + r'''
local function enter(address) local bp=assert(callbacks[address]); if bp.active then bp.callback() end end
enter(200); enter(700) -- frame 0
enter(100) -- mark is itself the first checkpoint
vsync(); assert(buttons[3] and not buttons[14])
enter(300); enter(700) -- actual adapter rand return hook
for i=1,5000 do vsync() end
enter(200) -- renew after the event callback has returned, on guest VSync entry
assert(created==2 and removed==1)
enter(400); vsync(); enter(400)
vsync(); assert(buttons[14] and not buttons[3])
local f=assert(io.open(work.."/state")); local rows=f:read("*a"); f:close()
assert(rows:find("mark 0 0 state TEST%[0%] 42"),rows)
assert(rows:find("mark 1 1 state TEST%[0%] 42"),rows)
assert(rows:find("1 rand 0 123"),rows)
assert(rows:find("loop#2 0 5002 state TEST%[0%] 42"),rows)
assert(rows:find("loop#2 1 5003 state TEST%[0%] 42"),rows)
quit(0)
''')
            command = ["xvfb-run", "-a", "pcsx-redux", "-no-ui", "-portable", str(work),
                       "-noupdate", "-no-webserver", "-no-gdb", "-dofile", str(script)]
            process = subprocess.run(command, capture_output=True, timeout=30, cwd=work)
            self.assertEqual(process.returncode, 0, (process.stdout, process.stderr))
            self.assertTrue((work / "state").is_file())

    @unittest.skipUnless(shutil.which("pcsx-redux") and shutil.which("xvfb-run"), "PCSX-Redux Lua runtime unavailable")
    def test_real_lua_pointer_ram_boundaries(self):
        write = Path.write_text
        for value, expected in ((0, "0"), (0x123, "0x80000123"),
                                (0x80000123, "0x80000123"), (0xa0000123, "0x80000123"),
                                (0x1f800000, None), (0x1f801810, None), (0x80200000, None)):
            with self.subTest(pointer=hex(value)):
                def script_with_pointer(path, text, *args, **kwargs):
                    if path.name == "mock.lua":
                        ram = ",".join(f"[{0x10000+i}]={(value >> (8*i)) & 255}" for i in range(4))
                        text = text.replace("return {[0x10000]=42}", "return {" + ram + "}")
                        text = text.replace('type="u8",address=0x80010000,count=1,width=1',
                                            'type="ptr",address=0x80010000,count=1,width=4')
                        if expected is not None:
                            text = text.replace("TEST%[0%] 42", "TEST%[0%] " + expected)
                        else:
                            text = text.replace("assert(code==0)", "assert(code==1)")
                            text = text[:text.index("enter(100) -- mark")]+"enter(100)\n" + r'''
local f=assert(io.open(work.."/progress")); local error=f:read("*a"); f:close()
assert(error:find("unresolved reference pointer"),error)
quit(0)
'''
                    return write(path, text, *args, **kwargs)
                with patch.object(Path, "write_text", script_with_pointer):
                    self.test_real_lua_checkpoint_callbacks_without_disc()

    @unittest.skipUnless(shutil.which("pcsx-redux") and shutil.which("xvfb-run"), "PCSX-Redux Lua runtime unavailable")
    def test_real_ffi_rand_return_breakpoints_without_disc(self):
        # A tiny synthetic PS1 program calls VSync, mark, then rand sixteen times.
        # Exercise real FFI handles and the pinned event dispatcher beyond its old
        # 21819-event overflow. Run events let the test advance without emulating
        # 30000 video frames; both names use the same createListener binding.
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            script = work / "ffi.lua"
            script.write_text("local work = " + json.dumps(str(work)) + "\n" + r'''
local ffi=require('ffi')
-- This test synchronously reenters Lua through FFI when emitting Run events.
require('jit').off()
local probe=PCSX.Events.createEventListener('GPU::Vsync',function() end)
probe:remove() -- exercise the pinned C++ removal binding outside its callback
local create, add = PCSX.Events.createEventListener, PCSX.addBreakpoint
local renewals, guestVsync = 0, nil
PCSX.Events.createEventListener=function(name, callback)
 assert(name=='GPU::Vsync'); renewals=renewals+1
 return create('ExecutionFlow::Run',callback)
end
PCSX.addBreakpoint=function(address, kind, width, cause, callback, ...)
 if cause=='DW1 first VSync' then guestVsync=callback end
 return add(address,kind,width,cause,callback,...)
end
local ram=ffi.cast('uint32_t*',PCSX.getMemPtr())
local words={0x0c004010,0,0x0c004014,0,0x24080010,0x0c004018,0,0x2508ffff,0x1500fffc,0,0x0800401c,0}
for i,v in ipairs(words) do ram[0x10000/4+i-1]=v end
for _,o in ipairs({0x40,0x50}) do ram[(0x10000+o)/4]=0x03e00008; ram[(0x10004+o)/4]=0 end
ram[0x10060/4]=0x2402007b;ram[0x10064/4]=0x03e00008;ram[0x10068/4]=0
ram[0x10100/4]=42
DW1_CAPTURE={mark=0x80010050,vsync=0x80010040,rand=0x80010060,
 progress=work..'/progress',state_trace=work..'/state',raw=work..'/raw',meta=work..'/meta',
 max_vblanks=100000,capture_vblank=20,inputs_from='mark',inputs={},
 checkpoints={{label='mark',address=0x80010050,occurrence=1,window=0},
              {label='end',address=0x80010070,occurrence=1,window=0}},
 state_vars={{name='TEST',type='u8',address=0x80010100,count=1,width=1}},state_symbols={}}
''' + "\ndofile(" + json.dumps(str(capture.ROOT / "tools/port/reference/capture.lua")) + ")\n" + r'''
DW1_TEST_STRESS=PCSX.addBreakpoint(0x80010010,'Exec',4,'event stress',function()
 for i=1,30000 do PCSX.pauseEmulator();PCSX.resumeEmulator();guestVsync() end
 assert(renewals>5)
 return true
end)
DW1_TEST_END=PCSX.addBreakpoint(0x80010070,'Exec',4,'done',function()
 PCSX.quit(0); return true
end)
PCSX.getRegisters().pc=0x80010000
''')
            process = subprocess.run(["xvfb-run", "-a", "pcsx-redux", "-no-ui", "-portable", str(work),
                                      "-interpreter", "-softgpu", "-debugger", "-noupdate",
                                      "-no-webserver", "-no-gdb", "-dofile", str(script), "-run"],
                                     capture_output=True, timeout=30, cwd=work)
            self.assertEqual(process.returncode, 0, (process.stdout, process.stderr))
            rows = (work / "state").read_text().splitlines()
            self.assertEqual(rows, ["mark 0 0 state TEST[0] 42"] +
                             [f"30000 rand {i} 123" for i in range(16)] + ["end 0 30000 state TEST[0] 42"])


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
        lua.with_suffix('.py').write_text('# adapter fixture')
        (self.scene.parent / 'run.py').write_text('# runner fixture')
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
if "--state" not in sys.argv:
    import re
    script = (work / "capture.lua").read_text()
    match = re.search(r'\\["state_trace"\\]="([^"]+)"', script)
    if match:
        text = "".join(f"{k} state TEST[0] -32768\\n{k} state POINTER[0] TABLE+4\\n" for k in range(1, 61))
        if mode in ("checkpoint", "state-time-different"):
            text = "".join(f"{cp} {k} {origin+k} state TEST[0] -32768\\n{cp} {k} {origin+k} state POINTER[0] TABLE+4\\n"
                           for cp, origin in (("titleMark", 0), ("titleMark#2", 10)) for k in range(2))
        if mode == "state-time-different" and work.name == "work-2": text = text.replace("titleMark#2 0 10", "titleMark#2 0 11").replace("titleMark#2 1 11", "titleMark#2 1 12")
        if mode == "state-different" and work.name == "work-2": text = text.replace("-32768", "0", 1)
        if mode != "state-missing": pathlib.Path(match[1]).write_text(text)
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
        runner_root_patch = patch.object(capture._runner, "ROOT", self.root)
        runner_root_patch.start()
        self.addCleanup(runner_root_patch.stop)
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
                self.run_capture("--timeout", "1")
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
                       [dict(vblank=1, buttons=[]), dict(vblank=0, buttons=[])], {}, None):
            with self.assertRaises(ValueError):
                capture.validate_scene(dict(capture_vblank=60, inputs=events))

    def state_fixture(self):
        self.symbols.write_text(self.symbols.read_text() +
            'rand = 0x80004000;\nTEST = 0x80100000;\nPOINTER = 0x80100004;\nTABLE = 0x80100008; // size:0x10\n')
        (self.scene.parent / "state_vars.txt").write_text('TEST s16\nPOINTER ptr\n')
        self.scene.write_text(self.original.replace('inputs = []',
            'inputs_from = "mark"\ninputs = [{vblank = 2, buttons = [3,14]}, {vblank = 4, buttons = []}]'))

    def test_state_capture_and_inputs(self):
        self.state_fixture()
        self.assertEqual(self.run_capture("--state"), 0)
        output = self.root / "build/reference/title"
        self.assertEqual((output / "state.sha256").read_text().strip(),
                         capture._runner.state_fingerprint(capture.tomllib.loads(self.scene.read_text())))
        script = (output / "work-1/capture.lua").read_text()
        self.assertIn('["inputs_from"]="mark"', script)
        self.assertIn('["buttons"]={3,14}', script)
        self.assertIn('["type"]="s16"', script)
        self.assertIn('["rand"]=2147500032', script)
        self.assertNotIn("reference_sha256", self.scene.read_text())

    def test_state_failure_preserves_traces_and_invalidates_cache(self):
        self.state_fixture()
        for mode, message in (("state-different", "non-deterministic state traces"),
                              ("state-missing", "run-1.state")):
            os.environ["FAKE_MODE"] = mode
            with self.assertRaises((ValueError, FileNotFoundError)) as error:
                self.run_capture("--state")
            self.assertIn(message, str(error.exception))
            self.assertFalse((self.root / "build/reference/title/state.sha256").exists())

    def test_state_stamp_requires_pinned_image_validation(self):
        self.state_fixture()
        self.scene.write_text(self.scene.read_text() + 'reference_sha256 = "' + '0' * 64 + '"\n')
        with self.assertRaisesRegex(ValueError, "reference hash mismatch"):
            self.run_capture("--state")
        self.assertFalse((self.root / "build/reference/title/state.sha256").exists())
        self.assertTrue((self.root / "build/reference/title/run-2.state").exists())

    def test_state_bios_and_runner_invalidate_fingerprint(self):
        self.state_fixture()
        scene = capture.tomllib.loads(self.scene.read_text())
        baseline = capture._runner.state_fingerprint(scene)
        self.assertEqual(self.run_capture("--state", "--bios", str(self.bios)), 0)
        stamp = self.root / "build/reference/title/state.sha256"
        self.assertNotEqual(stamp.read_text().strip(), baseline)
        self.assertEqual(stamp.read_text().strip(), capture._runner.state_fingerprint(
            scene, hashlib.sha256(self.bios.read_bytes()).hexdigest()))
        (self.scene.parent / "run.py").write_text('# changed runner')
        self.assertNotEqual(capture._runner.state_fingerprint(scene), baseline)

    def test_state_preflight_without_disc(self):
        self.state_fixture()
        os.environ.pop("DW1_DISC_DIR")
        for declaration, message in (("MISSING u8", "missing reference symbol: MISSING"),
                                     ("TEST float", "invalid state variable"),
                                     ("TEST u8[0]", "invalid state variable count")):
            (self.scene.parent / "state_vars.txt").write_text(declaration)
            with self.assertRaisesRegex(ValueError, message):
                self.run_capture("--state")

    def test_state_requires_rand_when_declared(self):
        self.state_fixture()
        self.scene.write_text(self.scene.read_text() + 'expect_rand = true\n')
        with self.assertRaisesRegex(ValueError, "at least 16 rand calls, got 0"):
            self.run_capture("--state")

    def test_checkpoint_capture_and_preflight(self):
        self.state_fixture()
        self.scene.write_text('mark = "titleMark"\ncapture_vblank = 60\ninputs_from = "mark"\n'
            'checkpoints = ["titleMark", "titleMark#2"]\nwindow = 1\n'
            'inputs = [{after = "titleMark#2", vblank = 0, buttons = [3]}]\n')
        os.environ["FAKE_MODE"] = "checkpoint"
        self.assertEqual(self.run_capture("--state"), 0)
        output = self.root / "build/reference/title"
        script = (output / "work-1/capture.lua").read_text()
        self.assertIn('["label"]="titleMark#2"', script)
        self.assertIn('["occurrence"]=2', script)
        self.assertIn('["window"]=1', script)
        self.assertIn('["after"]="titleMark#2"', script)
        stamp = (output / "state.sha256").read_text().strip()
        scene = capture.tomllib.loads(self.scene.read_text())
        scene["window"] = 2
        self.assertNotEqual(stamp, capture._runner.state_fingerprint(scene))
        os.environ["FAKE_MODE"] = "state-time-different"
        with self.assertRaisesRegex(ValueError, "non-deterministic state traces"):
            self.run_capture("--state")
        self.assertFalse((output / "state.sha256").exists())
        os.environ.pop("DW1_DISC_DIR")
        self.scene.write_text(self.scene.read_text().replace('"titleMark#2"', '"missing#2"'))
        with self.assertRaisesRegex(ValueError, "missing reference checkpoint symbol: missing"):
            self.run_capture("--state")

    def test_pointer_symbol_extents(self):
        self.assertEqual(capture.pointer_symbols('A = 0x80100000; // size:0x4\n'
            'B = 0x80100008;\nC = 0x80100010; // type:func\n'),
            [dict(name="A", address=0x80100000, size=4), dict(name="B", address=0x80100008, size=8)])


if __name__ == "__main__":
    unittest.main()

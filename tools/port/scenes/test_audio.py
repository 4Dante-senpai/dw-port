#!/usr/bin/env python3
"""L1 for rule B, checkpoint-relative timestamps and aligned five-second SNR."""
import importlib.util
import math
from pathlib import Path
import random
import tempfile
import unittest
import wave
from array import array
import sys
import tomllib

spec = importlib.util.spec_from_file_location("scene_audio", Path(__file__).with_name("audio.py"))
audio = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audio)


class AudioTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def trace(self, name, rows):
        path = self.root / name
        path.write_text("# dw1-spu-trace-v1\n" + rows)
        return path

    def wav(self, name, pcm, rate=44100):
        path = self.root / name
        data = array("h", pcm)
        if sys.byteorder != "little":
            data.byteswap()
        with wave.open(str(path), "wb") as stream:
            stream.setparams((2, 2, rate, 0, "NONE", "not compressed"))
            stream.writeframes(data.tobytes())
        return path

    def test_rule_b(self):
        ref = self.trace("ref", "0 0 000 1234\n0 0 004 5678\n0 1 000 1235\n1 735 188 0001\n")
        port = self.trace("port", "0 50 004 5678\n0 51 000 1234\n0 52 000 1235\n1 900 188 0001\n")
        self.assertEqual(audio.compare_trace(port, ref), dict(writes=4, sample_offset_max=165,
                                                            sample_offset_mean=79.25))
        # Order within one register, duplicates, missing/extra writes and VBlank matter.
        for rows in ("", "0 0 000 1235\n0 1 000 1234\n", "0 0 000 1234\n",
                     "0 0 000 1234\n0 1 000 1234\n", "1 0 000 1234\n"):
            with self.assertRaisesRegex(ValueError, "VBlank .*register.*port=.*reference="):
                audio.compare_trace(self.trace("bad", rows), ref)

    def test_invalid_trace(self):
        for rows in ("0 0 001 0000\n", "0 0 200 0000\n", "0 0 000 10000\n",
                     "1 2 000 0000\n0 3 000 0000\n", "0 5 000 0000\n1 4 000 0000\n",
                     "-1 0 000 0000\n", "0 -1 000 0000\n", "bogus\n"):
            with self.assertRaises(ValueError):
                audio.read_trace(self.trace("bad", rows))

    def test_correlation(self):
        rng = random.Random(35)
        ref = [rng.randint(-10000, 10000) for _ in range(4096 * 2)]
        for lag in (-127, 0, 139):
            actual = ([0] * (lag * 2) + ref if lag >= 0 else ref[-lag * 2:] + [0] * (-lag * 2))
            self.assertEqual(audio.alignment(actual, ref, 200), lag)
        # Anti-phase stereo must not cancel out during alignment.
        stereo = [c for sample in ref[::2] for c in (sample, -sample)]
        self.assertEqual(audio.alignment([0] * 34 + stereo, stereo, 100), 17)
        # FFT roundoff must not invent a lag when the correlation peak is flat.
        self.assertEqual(audio.alignment([10000] * 8192, [10000] * 8192, 200), 0)

    def test_windows(self):
        # Independently known error power: (10000 / 100)^2 -> exactly 40 dB.
        ref = [10000] * (audio.RATE * 11 * 2)
        reference = self.wav("ref.wav", ref)
        actual = self.wav("port.wav", [10100] * len(ref))
        result = audio.compare_pcm(actual, reference, max_offset=0)
        self.assertEqual([w["frames"] for w in result["windows"]],
                         [audio.WINDOW, audio.WINDOW, audio.RATE])
        self.assertAlmostEqual(result["worst_snr_db"], 40)
        ref[audio.WINDOW * 2:] = [10101] * (len(ref) - audio.WINDOW * 2)
        with self.assertRaisesRegex(ValueError, "worst window 5.000 s"):
            audio.compare_pcm(self.wav("bad.wav", ref), reference, max_offset=0)
        identical = audio.compare_pcm(reference, reference, max_offset=0)
        self.assertTrue(math.isinf(identical["worst_snr_db"]))

    def test_invalid_pcm_and_silence(self):
        short = self.wav("short.wav", [0] * 100)
        with self.assertRaisesRegex(ValueError, "5 s"):
            audio.compare_pcm(short, short)
        with self.assertRaisesRegex(ValueError, "44.1 kHz"):
            audio.read_pcm(self.wav("rate.wav", [0] * 100, 22050))
        silence = self.wav("silence.wav", [0] * (audio.WINDOW * 2))
        with self.assertRaisesRegex(ValueError, "SNR -inf"):
            audio.compare_pcm(self.wav("noise.wav", [1] * (audio.WINDOW * 2)), silence)
        with self.assertRaisesRegex(ValueError, "duration mismatch"):
            audio.compare_pcm(self.wav("long.wav", [0] * (audio.WINDOW * 2 + 200)), silence)

    def test_external_paths(self):
        repo = self.root / "repo"
        repo.mkdir()
        for path in (repo / "build/audio", repo / "../repo/assets"):
            with self.assertRaisesRegex(ValueError, "outside"):
                audio.external_output(path, repo)
        (self.root / "link").symlink_to(repo, target_is_directory=True)
        with self.assertRaises(ValueError):
            audio.external_output(self.root / "link/audio", repo)
        self.assertEqual(audio.external_output(self.root / "audio", repo), self.root / "audio")

    def test_scene_segments(self):
        scene = tomllib.loads(Path(__file__).with_name("title_audio.toml").read_text())
        self.assertEqual(audio.validate_segments(scene), scene["audio"])
        for key, value in (("audio", []), ("audio", [dict(start="gameLoop", vblanks=1200)] * 2),
                           ("inputs", scene["inputs"] + [dict(after="gameLoop", vblank=1, buttons=[])])):
            with self.assertRaises(ValueError):
                audio.validate_segments(dict(scene, **{key: value}))


if __name__ == "__main__":
    unittest.main()

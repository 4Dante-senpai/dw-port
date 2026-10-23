"""Exercise video failure, audio fallback and headless SDL bypass."""
import os
import subprocess
import sys
import tempfile

binary = sys.argv[1]
def run(video, audio, *args):
    return subprocess.run([binary, *args], env=dict(os.environ,
        SDL_VIDEODRIVER=video, SDL_AUDIODRIVER=audio),
        capture_output=True, text=True, timeout=20)

result = run("dw1-invalid-video", "dw1-invalid-audio", "--headless", "--frames", "1")
assert result.returncode == 0 and "reached frame 0" in result.stdout, result
assert "audio unavailable" not in result.stderr, result
result = run("dw1-invalid-video", "dummy", "--frames", "1")
assert result.returncode == 1 and "SDL video" in result.stderr, result
result = run("offscreen", "dw1-invalid-audio", "--frames", "120")
assert result.returncode == 0 and "audio unavailable" in result.stderr, result
with tempfile.TemporaryDirectory() as directory:
    result = run("offscreen", "dummy", "--pack", os.path.join(directory, "missing.pack"))
    assert result.returncode == 1, result
print("sdl_cli: headless bypass, video exit 1, audio failure fallback and startup-return cleanup OK")

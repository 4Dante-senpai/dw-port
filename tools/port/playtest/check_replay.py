"""Replays a versioned synthetic inputs file with run.py and checks its final digest.

usage: check_replay.py BUILD INPUTS VBLANKS DIGEST OUTPUT   (exit 77 propagates run.py's skip)
"""
import json, subprocess, sys
from pathlib import Path

build, inputs, vblanks, digest, output = sys.argv[1:6]
run = Path(__file__).with_name('run.py')
rc = subprocess.call([sys.executable, '-I', '-B', str(run), '--build', build, '--inputs', inputs,
                      '--vblanks', vblanks, '--output', output])
if rc:
    sys.exit(rc)
got = json.load(open(Path(output) / 'report.json'))['trials'][0]['runs'][0]['digest']
if got != digest:
    sys.exit(f'digest mismatch: got {got}, expected {digest}')

#!/usr/bin/env python3
"""Host failures for a missing/short executable; synthetic Pack only (CTest fixture)."""
import hashlib
import pathlib
import struct
import subprocess
import sys
import tempfile

exe, repo, build = map(pathlib.Path, sys.argv[1:])
original = bytearray((build / 'pack_t1.pack').read_bytes())
# pack_open trusts this build's Manifest digest; no copyrighted bytes are needed to reach
# the libgte loader. The index/data remain from the small synthetic disc in test_pack.c.
original[16:48] = hashlib.sha256((repo / 'tools/port/manifest/us.tsv').read_bytes()).digest()
index = struct.unpack_from('<I', original, 48)[0]
with tempfile.TemporaryDirectory(dir=build) as tmp:
    for missing in (False, True):
        data = original.copy()
        if missing:
            assert data[index:index+14] == b'\\SLUS_010.32;1'
            data[index:index+14] = b'\\NONE_010.32;1'
        path = pathlib.Path(tmp) / 'synthetic.pack'
        path.write_bytes(data)
        run = subprocess.run([str(exe), '--pack', str(path), '--frames', '1'],
                             capture_output=True, text=True, timeout=30)
        assert run.returncode == 1, (run.returncode, run.stdout, run.stderr)
        assert 'libgte: cannot load rsin_tbl' in run.stderr, run.stderr
        assert 'missing or short' in run.stderr, run.stderr
        print(f'libgte_host: {"missing" if missing else "short"} EXE: exit 1, loader diagnostic OK')

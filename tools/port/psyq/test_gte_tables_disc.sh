#!/bin/sh
# Class B: independent raw-disc extraction vs hashes of the loaded runtime tables.
# Usage: test_gte_tables_disc.sh <dw1> <repo> <build dir>
set -eu
[ -n "${DW1_DISC_DIR:-}" ] || { echo "gte_tables_disc: DW1_DISC_DIR not set, skipped"; exit 77; }
dw1=$1
repo=$2
build=$3
"$dw1" --headless --pack "$build/us.pack" --frames 1 > "$build/gte_tables_disc.out" 2>&1 || {
    cat "$build/gte_tables_disc.out"; exit 1;
}
python3 - "$repo" "$build/gte_tables_disc.out" <<'PY'
import hashlib
import os
import pathlib
import re
import sys
import yaml

repo, log = map(pathlib.Path, sys.argv[1:])
# Do not use the Pack reader or the table generator under test for the expected bytes.
rows = (repo / 'tools/port/manifest/us.tsv').read_text().splitlines()
exe = next(row.split('\t') for row in rows if row.startswith('@exe\t'))
lba, size = int(exe[1]), int(exe[4])
disc = pathlib.Path(os.environ['DW1_DISC_DIR'])
cue = sorted(disc.glob('*.cue'))[0]
filename = re.search(r'FILE "([^"]+)" BINARY', cue.read_text())[1]
with (disc / filename).open('rb') as f:
    data = bytearray()
    for i in range((size + 2047) // 2048):
        f.seek((lba + i) * 2352 + 24)
        sector = f.read(2048)
        assert len(sector) == 2048
        data.extend(sector)
    data = data[:size]
assert hashlib.sha256(data).hexdigest() == exe[5], 'raw EXE extraction disagrees with Manifest'
symbols = dict((n, int(a, 16)) for n, a in re.findall(
    r'^\s*(\w+)\s*=\s*(0x[0-9A-Fa-f]+);', (repo / 'config/us/symbols.txt').read_text(), re.M))
main = next(s for s in yaml.safe_load((repo / 'config/us/main.yaml').read_text())['segments']
            if isinstance(s, dict) and s.get('name') == 'main')
actual = re.findall(r'^port: libgte table (\w+) sha256 ([0-9a-f]{64})$', log.read_text(), re.M)
assert len(actual) == 4 and len(dict(actual)) == 4
for name, digest in actual:
    assert name in ('rsin_tbl', 'SQRT', 'rcossin_tbl', 'ratan_tbl')
    address = symbols[name]
    end = min(a for a in symbols.values() if a > address)
    offset = address - main['vram'] + main['start']
    expected = hashlib.sha256(data[offset:offset + end - address]).hexdigest()
    assert digest == expected, (name, digest, expected)
    print(f'gte_tables_disc: {name} sha256 {digest} OK')
PY

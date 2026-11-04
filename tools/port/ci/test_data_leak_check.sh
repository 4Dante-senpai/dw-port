#!/bin/sh
set -eu
exec python3 - "$@" <<'PY'
import os
from pathlib import Path
import subprocess
import sys
import tempfile

mode, tool = sys.argv[1:3]
def run(pack, exceptions, artifacts, expected, linker_map=None):
    options = ['--wasm-map', str(linker_map)] if linker_map else []
    result = subprocess.run([tool, *options, '--disc', str(pack), '--exception',
                             *map(str, exceptions), '--', *map(str, artifacts)],
                            capture_output=True, text=True)
    print(result.stdout, end='')
    print(result.stderr, end='', file=sys.stderr)
    if result.returncode != expected:
        sys.exit(f'expected exit {expected}, got {result.returncode}')
    return result.stdout

def uleb(value):
    result = bytearray()
    while True:
        byte = value & 127
        value >>= 7
        result.append(byte | (128 if value else 0))
        if not value:
            return bytes(result)

def row(address, offset, size, name, indent=0):
    return f'{address:>8} {offset:8x} {size:8x} ' + ' ' * indent + name + '\n'

def wasm_segments(data):
    def read(pos):
        value = shift = 0
        while True:
            byte = data[pos]; pos += 1
            value |= (byte & 127) << shift; shift += 7
            if byte < 128:
                return value, pos
    pos = 8
    while pos < len(data):
        kind = data[pos]; size, pos = read(pos + 1); end = pos + size
        if kind == 11:
            count, pos = read(pos)
            result = []
            for _ in range(count):
                flag, pos = read(pos)
                assert flag == 1
                size, pos = read(pos)
                result.append((pos, size)); pos += size
            assert pos == end
            return result
        pos = end
    raise AssertionError('missing data section')

with tempfile.TemporaryDirectory(prefix='dw1-leak-') as tmp:
    tmp = Path(tmp)
    artifact, exception, pack = (tmp / name for name in ('port.o', 'source.o', 'us.pack'))
    if mode == 'synthetic':
        a, b = bytes(range(64)), bytes(range(64, 128))
        pack.write_bytes(a + b + bytes(64) + bytes([0, 1, 2, 3]) * 16)
        # Separate trivial files avoid nontrivial windows crossing the padding boundary.
        artifact.write_bytes(bytes(64))
        run(pack, [], [artifact], 0)
        artifact.write_bytes(bytes([0, 1, 2, 3]) * 16)
        run(pack, [], [artifact], 0)
        for short in (b'', a[:63]):
            pack.write_bytes(short)
            run(pack, [], [artifact], 2)
        pack.write_bytes(a + b + bytes(64) + bytes([0, 1, 2, 3]) * 16)
        artifact.write_bytes(a + b)
        exception.write_bytes(a)
        other = tmp / 'other.o'; other.write_bytes(b)
        # Boundary windows are covered byte by byte, even though absent from either exception.
        run(pack, [exception, other], [artifact], 0)
        output = run(pack, [exception], [artifact], 1)
        assert str(artifact) in output and 'offset=' in output
        # Coverage from exactly 16-byte fragments (none contains 64 bytes).
        artifact.write_bytes(a)
        fragments = []
        for i in range(0, 64, 16):
            fragment = tmp / f'fragment-{i}.o'
            fragment.write_bytes(a[i:i+16])
            fragments.append(fragment)
        run(pack, fragments, [artifact], 0)
        fragments[-1].write_bytes(a[48:63])
        run(pack, fragments, [artifact], 1)
        # A 15-byte exception never covers a byte.
        exception.write_bytes(a[:15])
        run(pack, [exception], [artifact], 1)
        exception.write_bytes(a[:-1])
        run(pack, [exception], [artifact], 1)
        run(pack, [], [tmp / 'missing'], 2)
        # Five values is nontrivial; four is trivial.
        artifact.write_bytes(bytes(range(5)) * 13)
        pack.write_bytes(artifact.read_bytes())
        run(pack, [], [artifact], 1)
        # Trivial exception windows also contribute byte coverage.
        trivial = bytes([0, 1, 2, 3]) * 16
        artifact.write_bytes(a + trivial)
        pack.write_bytes(a + trivial)
        exception.write_bytes(a)
        other.write_bytes(trivial)
        run(pack, [exception, other], [artifact], 0)
        # Exact rolling-hash collision: balanced base-257 digits of 2**64.
        # Neither exception coverage nor disc membership may trust the hash alone.
        number, digits = 1 << 64, []
        while number:
            digit = number % 257
            if digit > 128:
                digit -= 257
            digits.append(digit)
            number = (number - digit) // 257
        digits = list(reversed(digits))
        prefix = bytes(range(64 - len(digits)))
        left = prefix + bytes(max(0, -d) for d in digits)
        right = prefix + bytes(max(0, d) for d in digits)
        def rolling(data):
            value = 0
            for byte in data:
                value = (value * 257 + byte) & ((1 << 64) - 1)
            return value
        assert left != right and rolling(left) == rolling(right)
        artifact.write_bytes(left)
        exception.write_bytes(right)
        pack.write_bytes(left)
        run(pack, [exception], [artifact], 1)
        pack.write_bytes(right)
        run(pack, [], [artifact], 0)
        # Map attribution overrides byte coverage inside DATA, including a single
        # non-game byte; only zero gaps are allowed. Offsets differ from wasm.
        wasm, linker_map = tmp / 'dw1.wasm', tmp / 'dw1.map'
        game = tmp / 'game.o'; game.write_bytes(a)
        port = tmp / 'port-input.o'; port.write_bytes(a)
        payload = uleb(2) + b'\x01' + uleb(64) + a + b'\x01' + uleb(64) + b
        wasm.write_bytes(b'\0asm\1\0\0\0' + b'\x0b' + uleb(len(payload)) + payload)
        pack.write_bytes(a + b)
        header = '    Addr      Off     Size Out     In      Symbol\n'
        base = (header + row('-', 0x1000, len(payload), 'DATA') +
                row('10000', 0x1001, 64, '.data') +
                row('10000', 0x1003, 64, 'game.o:(.data.a)', 8) +
                row('10000', 0x1003, 32, 'a', 16) +
                row('10040', 0x1043, 64, '.tdata') +
                row('0', 0x1045, 64, 'game.o:(.tdata.b)', 8))
        linker_map.write_text(base)
        run(pack, [game], [wasm], 0, linker_map)
        # Missing ownership must not inherit 16-byte exception coverage.
        gap = base.replace(row('10000', 0x1003, 64, 'game.o:(.data.a)', 8),
                           row('10000', 0x1003, 32, 'game.o:(.data.a)', 8))
        linker_map.write_text(gap)
        run(pack, [game], [wasm], 1, linker_map)
        # Zero padding without a row remains covered.
        padded = uleb(2) + b'\x01' + uleb(64) + a[:32] + bytes(32) + b'\x01' + uleb(64) + b
        padded_wasm = b'\0asm\1\0\0\0' + b'\x0b' + uleb(len(padded)) + padded
        original_wasm = wasm.read_bytes()
        wasm.write_bytes(padded_wasm)
        pack.write_bytes(a[:32] + bytes(32) + b)
        run(pack, [game], [wasm], 0, linker_map)
        wasm.write_bytes(original_wasm)
        pack.write_bytes(a + b)
        # Non-game attribution is a leak even when the exception contains it.
        contaminated = base.replace('game.o:(.tdata.b)', 'port-input.o:(.tdata.b)')
        linker_map.write_text(contaminated)
        run(pack, [game, port], [wasm], 0, linker_map)  # explicit object whitelist
        output = run(pack, [game], [wasm], 1, linker_map)
        assert str(wasm) in output and 'offset=' in output
        one_byte = base.replace(row('10000', 0x1003, 64, 'game.o:(.data.a)', 8),
                                row('10000', 0x1003, 32, 'game.o:(.data.a)', 8) +
                                row('10020', 0x1023, 1, 'port-input.o:(.data.byte)', 8) +
                                row('10021', 0x1024, 31, 'game.o:(.data.tail)', 8))
        linker_map.write_text(one_byte)
        run(pack, [game], [wasm], 1, linker_map)
        # Bytes outside DATA still require 16-byte exception coverage.
        custom = b'\0' + uleb(65) + b'\0' + a
        wasm.write_bytes(wasm.read_bytes() + custom)
        linker_map.write_text(base)
        run(pack, [], [wasm], 1, linker_map)
        run(pack, [game], [wasm], 0, linker_map)
        for invalid in ('', base.replace('      40 .data', '      41 .data'),
                        base.replace('    1003', '    ffff'),
                        base + row('40', 0x1045, 64, 'port-input.o:(.overlap)', 8),
                        base.replace('game.o:(.tdata.b)', 'missing-owner')):
            linker_map.write_text(invalid)
            run(pack, [game], [wasm], 2, linker_map)
        run(pack, [game], [wasm], 2, tmp / 'missing.map')
        linker_map.write_text(base)
        original = wasm.read_bytes()
        wasm.write_bytes(original[:-1])
        run(pack, [game], [wasm], 2, linker_map)
        wasm.write_bytes(b'not a wasm')
        run(pack, [game], [wasm], 2, linker_map)
        print('data_leak_check_synthetic: OK')
    elif mode == 'contaminated':
        build = Path(sys.argv[3]).resolve()
        real = build / 'us.pack'
        if not os.environ.get('DW1_DISC_DIR') or not real.is_file():
            print('web_data_leak_contaminated: DW1_DISC_DIR or us.pack missing; skipped')
            sys.exit(77)
        objects = sorted(p for p in (build / 'CMakeFiles').rglob('*.o')
                         if 'dw1_game_objs.dir' not in p.parts and 'port' in p.relative_to(build / 'CMakeFiles').parts)
        if not objects:
            sys.exit('no port object found')
        # Select at runtime, never persist a disc fixture in the repository or logs.
        with real.open('rb') as stream:
            window = stream.read(64)
            while len(window) == 64 and len(set(window)) < 16:
                byte = stream.read(1)
                if not byte:
                    break
                window = window[1:] + byte
        if len(window) != 64 or len(set(window)) < 16:
            sys.exit('no suitable contamination window')
        data = objects[0].read_bytes()
        artifact.write_bytes(data + window)
        output = run(real, [], [artifact], 1)
        assert str(artifact) in output and f'offset={len(data)} ' in output
        # Keep the real wasm unchanged; reattribute a game input that matches
        # the Pack to an existing port object in a temporary copy of its map.
        import mmap
        import re
        wasm = build / 'dw1.wasm'
        wasm_data = wasm.read_bytes()
        segments = wasm_segments(wasm_data)
        lines = (build / 'dw1.map').read_text().splitlines(keepends=True)
        segment = -1
        in_data = False
        selected = None
        with real.open('rb') as stream, mmap.mmap(stream.fileno(), 0, access=mmap.ACCESS_READ) as disc:
            for index, line in enumerate(lines):
                fields = list(re.finditer(r'\S+', line))
                if len(fields) != 4:
                    continue
                addr, offset, size, name = (f.group() for f in fields)
                column = fields[3].start()
                if column == 27 and name == 'DATA':
                    in_data = True
                    continue
                if not in_data:
                    continue
                if column == 27:
                    if name == '.bss' or name.startswith('CUSTOM('):
                        break
                    segment += 1
                    length = int(size, 16)
                    assert length == segments[segment][1], (
                        f'{build / "dw1.map"}: segment={segment} map_size={length} '
                        f'wasm_size={segments[segment][1]}')
                    map_payload = int(offset, 16) + 1 + len(uleb(length))
                elif column == 35 and name.startswith('CMakeFiles/dw1_game_objs.dir/') and int(size, 16) >= 64:
                    off = segments[segment][0] + int(offset, 16) - map_payload
                    candidate = wasm_data[off:off+64]
                    if len(set(candidate)) >= 16 and disc.find(candidate) >= 0:
                        selected = index, off, name
                        break
        assert selected is not None, 'no mapped game range suitable for contamination'
        index, off, name = selected
        lines[index] = lines[index].replace(name.split(':(', 1)[0], str(objects[0]))
        linker_map = tmp / 'dw1.map'
        linker_map.write_text(''.join(lines))
        # Resolve all unchanged relative input paths against the real build.
        (tmp / 'CMakeFiles').symlink_to(build / 'CMakeFiles', target_is_directory=True)
        game = sorted((build / 'CMakeFiles/dw1_game_objs.dir').rglob('*.o'))
        output = run(real, game, [wasm], 1, linker_map)
        assert str(wasm) in output and f'offset={off} ' in output
        print('web_data_leak_contaminated: OK')
    else:
        sys.exit('unknown mode')
PY

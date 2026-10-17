#!/usr/bin/env python3
"""Inventory callback casts, non-prototype calls, arity errors and wasm linker ABIs.

The preserved PS1 declarations are audited with DW1_ABI_AUDIT. Indirect casts use
Emscripten emulation; direct ABI corrections must disappear in the production link.
Both native and web use the pinned Emscripten frontend for stable diagnostics.
"""
import argparse
import concurrent.futures
import csv
import io
import json
from pathlib import Path
import re
import shlex
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[3]
TABLE = ROOT / 'tools/port/metrics/fnptr_casts.tsv'
# Legacy diagnostic wording is intentionally excluded from TSV identity.

DIAG = re.compile(r'^(.*?):(\d+):(\d+): (?:warning|error): (.*?)$', re.M)

def scan(command, audit):
    args = shlex.split(command['command'])
    # Keep the actual TU, generated overlay wrapper, include paths and semantic flags.
    for flag in ('-o', '-c'):
        at = args.index(flag)
        if flag == '-o': del args[at:at+2]
        else: del args[at]
    args = ['emcc'] + [a for a in args[1:] if a not in ('-w', '-m32', '-fno-pie')
                      and not a.startswith(('-fsanitize=', '-fno-sanitize=', '-fno-sanitize-recover='))]
    args += ['-fsyntax-only', '-Wno-error', '-Wcast-function-type-strict',
             '-Wincompatible-function-pointer-types', '-Wdeprecated-non-prototype',
             '-Wno-error=incompatible-function-pointer-types', '-fno-color-diagnostics']
    if audit: args.append('-DDW1_ABI_AUDIT')
    result = subprocess.run(args, cwd=command['directory'], capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(result.stderr)
    rows = []
    for file, line, col, message in DIAG.findall(result.stderr):
        if not any(term in message for term in ('cast-function-type-strict',
            'incompatible-function-pointer-types', 'passing arguments to', 'too few arguments', 'too many arguments')):
            continue
        kind = ('arity' if 'too few arguments' in message or 'too many arguments' in message else
                'callback' if 'function-pointer-types' in message or 'cast-function-type-strict' in message else 'non-prototype')
        file = str(Path(file).resolve().relative_to(ROOT))
        expression = (ROOT / file).read_text().splitlines()[int(line)-1][int(col)-1:].strip()
        # The source expression and column distinguish multiple casts on one line;
        # diagnostics wording can differ between the pinned Clang and Emscripten Clang.
        rows.append((kind, file, line, col, expression))
    return rows

def link_findings(build, commands):
    """Recompile preserved old declarations and inventory wasm-ld ABI diagnostics."""
    audit_dir = build.resolve() / 'fnptr-audit'
    audit_dir.mkdir(exist_ok=True)
    replacements = {}
    def compile_one(item):
        index, command = item
        args = shlex.split(command['command'])
        at = args.index('-o') + 1
        original = args[at]
        output = str(audit_dir / f'{index}.o')
        args[at] = output
        args.append('-DDW1_ABI_AUDIT')
        result = subprocess.run(args, cwd=command['directory'], capture_output=True, text=True)
        if result.returncode: raise RuntimeError(result.stderr)
        return original, output
    with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
        replacements.update(pool.map(compile_one, enumerate(commands)))
    ninja = subprocess.run(['ninja', '-t', 'commands', 'dw1'], cwd=build,
                           capture_output=True, text=True, check=True).stdout.splitlines()[-1]
    args = shlex.split(ninja)
    args = args[args.index('&&') + 1:]
    args = args[:args.index('&&')]
    args = [replacements.get(a, a) for a in args if a != '-Wl,--fatal-warnings']
    args.append('-Wl,--error-limit=0')
    args[args.index('-o') + 1] = str(audit_dir / 'dw1.js')
    result = subprocess.run(args, cwd=build, capture_output=True, text=True)
    (audit_dir / 'link.log').write_text(result.stderr)
    if result.returncode: raise RuntimeError(result.stderr)
    findings = []
    pattern = r'function signature mismatch: (\w+)\n>>> defined as (.*?) in .*?\n>>> defined as (.*?) in '
    for name, caller, callee in re.findall(pattern, result.stderr):
        findings.append(('wasm-ld', name, '0', '0', caller + ' / ' + callee,
                         ('DW1_PORT guarded missing animation argument; gameplay fidelity pending'
                          if name == 'setActiveAnim' else
                          ('port/psyq/include/memory.h: host bzero returns void' if name == 'bzero' else
                           'DW1_PORT direct ABI correction (preserves consumed arguments)'))))
    return sorted(set(findings))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('build', type=Path)
    parser.add_argument('--update', action='store_true')
    args = parser.parse_args()
    commands = json.loads((args.build / 'compile_commands.json').read_text())
    commands = [c for c in commands if 'dw1_game_objs.dir' in c['command']]
    if not commands: sys.exit('fnptr_inventory: no game TUs')
    with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
        production = set(row for found in pool.map(lambda c: scan(c, False), commands) for row in found)
        original = set(row for found in pool.map(lambda c: scan(c, True), commands) for row in found)
    unresolved = [r for r in production if r[0] == 'arity']
    if unresolved: sys.exit(f'fnptr_inventory: unresolved direct arity: {unresolved}')
    rows = sorted(original | production)
    output = io.StringIO()
    writer = csv.writer(output, delimiter='\t', lineterminator='\n')
    writer.writerow(('kind', 'file', 'line', 'column', 'expression', 'resolution'))
    for row in rows:
        resolution = ('EMULATE_FUNCTION_POINTER_CASTS (web); cdecl (native)' if row[0] == 'callback' else
                      'DW1_PORT direct ABI correction' if row not in production else
                      'C default argument promotions; direct ABI checked by wasm-ld')
        writer.writerow((*row, resolution))
    if 'emcc' in commands[0]['command']:
        for row in link_findings(args.build, commands): writer.writerow(row)
    elif TABLE.exists():
        for row in csv.DictReader(TABLE.open(), delimiter='\t'):
            if row['kind'] == 'wasm-ld': writer.writerow(row.values())
    if TABLE.exists() and not args.update:
        for row in csv.DictReader(TABLE.open(), delimiter='\t'):
            if not row['resolution'] or 'unresolved' in row['resolution'].lower():
                sys.exit('fnptr_inventory: unresolved row')
    expected = output.getvalue()
    if args.update: TABLE.write_text(expected)
    elif not TABLE.exists() or TABLE.read_text() != expected:
        sys.exit('fnptr_inventory: drift; review changes and run inventory.py BUILD --update')
    print(f'fnptr_inventory: {len(rows)} resolved casts, {len(commands)} TUs, zero unresolved direct arity diagnostics')

if __name__ == '__main__': main()

#!/usr/bin/env python3
"""PsyQ functions called by the game code, measured from the splat disassembly.

Usage (after `make regenerate`): tools/port/metrics/psyq_usage.py [VERSION]

PsyQ functions are the glabels of asm/<VERSION>/main/psyq/<lib>.s. A call site is a `jal` (or a
`j` tail call) to one of them from a game function (the per-function files splat writes for the
C subsegments, i.e. the original compiled code). References that are not calls (`%hi`/`%lo`
address loads of a PsyQ function) are counted separately. The last column says whether
reference/dw2-pc/psyq defines a function of that name (C definition at the start of a line).
"""
import collections
import glob
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
GLABEL = re.compile(r"^glabel (\S+)", re.M)
CALL = re.compile(r"\s(jal|j)\s+(\w+)\s*$", re.M)
ADDR = re.compile(r"%(?:hi|lo)\((\w+)\)")


def main():
    version = sys.argv[1] if len(sys.argv) > 1 else "us"
    asm = os.path.join(ROOT, "asm", version)
    lib_of = {}
    for path in sorted(glob.glob(os.path.join(asm, "main", "psyq", "*.s"))):
        lib = os.path.basename(path)[:-2]
        with open(path) as f:
            for name in GLABEL.findall(f.read()):
                lib_of[name] = lib

    calls = collections.Counter()
    addr_refs = collections.Counter()
    callers = collections.defaultdict(set)
    for path in glob.glob(os.path.join(asm, "**", "*.s"), recursive=True):
        rel = os.path.relpath(path, asm)
        if "matchings" not in rel.split(os.sep) and not re.match(r"main/(vs|shop)/[^/]+/", rel):
            continue
        unit = rel.split(os.sep)[0] if not rel.startswith(("main/vs/", "main/shop/")) else rel.split(os.sep)[1]
        with open(path) as f:
            text = f.read()
        for _, target in CALL.findall(text):
            if target in lib_of:
                calls[target] += 1
                callers[target].add(unit)
        for target in ADDR.findall(text):
            if target in lib_of:
                addr_refs[target] += 1
                callers[target].add(unit)

    dw2 = ""
    for path in glob.glob(os.path.join(ROOT, "reference", "dw2-pc", "psyq", "*.c")):
        with open(path, errors="replace") as f:
            dw2 += f.read()

    def in_dw2(name):
        return re.search(r"^[A-Za-z_][\w \*]*\b" + re.escape(name) + r"\s*\([^;]*$", dw2, re.M) is not None

    by_lib = collections.defaultdict(list)
    for name in set(calls) | set(addr_refs):
        by_lib[lib_of[name]].append(name)
    defined = collections.Counter(lib_of.values())

    print("| Librería | Funciones en el binario | Funciones llamadas por el juego | Llamadas (jal/j) | Referencias por dirección | Definidas en DW2-PC |")
    print("|---|---:|---:|---:|---:|---:|")
    tf = tc = ta = td = 0
    for lib in sorted(defined):
        names = by_lib.get(lib, [])
        c = sum(calls[n] for n in names)
        a = sum(addr_refs[n] for n in names)
        d = sum(in_dw2(n) for n in names)
        print(f"| {lib} | {defined[lib]} | {len(names)} | {c} | {a} | {d} |")
        td += d
        tf += len(names)
        tc += c
        ta += a
    print(f"| **total** | {sum(defined.values())} | {tf} | {tc} | {ta} | {td} |")
    for lib in sorted(by_lib):
        print(f"\n### {lib}\n")
        print("| Función | Llamadas | Ref. dirección | Unidades que la usan | En DW2-PC |")
        print("|---|---:|---:|---|---|")
        for n in sorted(by_lib[lib], key=lambda n: (-calls[n], n)):
            dw = "sí" if in_dw2(n) else "no"
            print(f"| `{n}` | {calls[n]} | {addr_refs[n]} | {', '.join(sorted(callers[n]))} | {dw} |")


if __name__ == "__main__":
    main()

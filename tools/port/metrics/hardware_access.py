#!/usr/bin/env python3
"""Fixed-address and coprocessor accesses in the game code, measured from the splat disassembly.

Usage (after `make regenerate`): tools/port/metrics/hardware_access.py [VERSION]

Scans the per-function files of the game (C subsegments) for:
- `lui` of an address that splat could not symbolize: scratchpad (0x1F800000-0x1F8003FF),
  hardware registers (0x1F801000-0x1F802FFF) and main RAM (0x80000000-0x801FFFFF; candidates,
  a `lui` of 0x80000000 is often a sign-bit constant, not an address);
- GTE (COP2) instructions, which the decompiled C emits through gte.h inline asm / macros.
Prints one table per class with per-unit counts, then the most frequent raw addresses.
"""
import collections
import glob
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
INSN = re.compile(r"^\s*/\* [0-9A-F]+ [0-9A-F]+ [0-9A-F]{8} \*/\s+(\S+)\s*(.*)$")
LUI = re.compile(r"\(0x([0-9A-Fa-f]+) >> 16\)")
COP2 = {"mtc2", "mfc2", "ctc2", "cfc2", "lwc2", "swc2"}


def classify(addr):
    if 0x1F800000 <= addr < 0x1F800400:
        return "scratchpad"
    if 0x1F801000 <= addr < 0x1F803000:
        return "hw_reg"
    if 0x80000000 <= addr < 0x80200000:
        return "ram_abs"
    return None


def main():
    version = sys.argv[1] if len(sys.argv) > 1 else "us"
    asm = os.path.join(ROOT, "asm", version)
    counts = collections.defaultdict(collections.Counter)  # class -> unit -> refs
    funcs = collections.defaultdict(lambda: collections.defaultdict(set))
    addrs = collections.defaultdict(collections.Counter)
    for path in glob.glob(os.path.join(asm, "**", "*.s"), recursive=True):
        rel = os.path.relpath(path, asm)
        parts = rel.split(os.sep)
        if "matchings" in parts:
            unit = parts[0]
        elif re.match(r"main/(vs|shop)/[^/]+/", rel):
            unit = parts[1]
        else:
            continue
        func = os.path.basename(path)[:-2]
        with open(path) as f:
            for line in f:
                m = INSN.match(line)
                if not m:
                    continue
                op, args = m.groups()
                if op == "lui":
                    a = LUI.search(args)
                    if a:
                        cls = classify(int(a.group(1), 16))
                        if cls:
                            counts[cls][unit] += 1
                            funcs[cls][unit].add(func)
                            addrs[cls][int(a.group(1), 16)] += 1
                elif op in COP2 or op.isupper():
                    counts["gte"][unit] += 1
                    funcs["gte"][unit].add(func)

    titles = {
        "scratchpad": "Scratchpad (0x1F800000-0x1F8003FF)",
        "hw_reg": "Registros de hardware (0x1F801000-0x1F802FFF)",
        "ram_abs": "Direcciones absolutas de RAM sin símbolo (0x80000000-0x801FFFFF)",
        "gte": "Instrucciones GTE (COP2)",
    }
    for cls, title in titles.items():
        print(f"\n### {title}\n")
        if not counts[cls]:
            print("Sin accesos.")
            continue
        print("| Unidad | Referencias | Funciones |")
        print("|---|---:|---:|")
        for unit in sorted(counts[cls]):
            print(f"| {unit} | {counts[cls][unit]} | {len(funcs[cls][unit])} |")
        print(f"| **total** | {sum(counts[cls].values())} | {sum(len(v) for v in funcs[cls].values())} |")
        if addrs[cls]:
            top = ", ".join(f"`0x{a:08X}` ({n})" for a, n in addrs[cls].most_common(12))
            print(f"\nDirecciones más frecuentes: {top}")


if __name__ == "__main__":
    main()

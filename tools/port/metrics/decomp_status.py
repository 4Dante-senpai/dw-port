#!/usr/bin/env python3
"""Decompilation status per executable / overlay, measured from the splat output.

Usage (after `make regenerate`): tools/port/metrics/decomp_status.py [VERSION]

Without the matching compiler there is no objdiff report, so a function counts as "C" when its
splat subsegment is of type `c` and its name is defined in the C source, and as "asm" when its
subsegment is `asm` (or it is missing from the C source). Sizes come from the
`nonmatching NAME, 0xSIZE` markers splat writes for every function (original byte sizes).
"""
import os
import re
import sys

import yaml

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
MARK = re.compile(r"^nonmatching (\S+), 0x([0-9A-Fa-f]+)", re.M)


def functions(path):
    with open(path) as f:
        return [(n, int(s, 16)) for n, s in MARK.findall(f.read())]


def c_defines(src, name):
    return re.search(r"^[^\s#/].*\b" + re.escape(name) + r"\s*\([^;]*$", src, re.M) is not None


def segment_status(cfg_path):
    with open(cfg_path) as f:
        cfg = yaml.safe_load(f)
    opts = cfg["options"]
    asm_path = os.path.join(ROOT, opts["asm_path"])
    src_path = os.path.join(ROOT, opts["src_path"])
    rows = {}
    for seg in cfg["segments"]:
        if not isinstance(seg, dict) or seg.get("type") != "code":
            continue
        for sub in seg.get("subsegments", []):
            if not isinstance(sub, list) or len(sub) < 3 or sub[1] not in ("c", "asm"):
                continue
            kind, name = sub[1], sub[2]
            lib = "psyq" if name.startswith("psyq/") else "game"
            if kind == "asm":
                funcs = functions(os.path.join(asm_path, name + ".s"))
                in_c = []
            else:
                d = os.path.normpath(os.path.join(asm_path, "matchings", name))
                funcs = [f for fn in sorted(os.listdir(d)) if fn.endswith(".s")
                         for f in functions(os.path.join(d, fn))]
                with open(os.path.normpath(os.path.join(src_path, name + ".c"))) as f:
                    src = f.read()
                in_c = [f for f in funcs if c_defines(src, f[0])]
            unit = os.path.normpath(os.path.join(opts["src_path"], name)).split(os.sep)[1]
            r = rows.setdefault((unit, lib), [0, 0, 0, 0])
            r[0] += len(in_c)
            r[1] += sum(s for _, s in in_c)
            r[2] += len(funcs) - len(in_c)
            r[3] += sum(s for _, s in funcs) - sum(s for _, s in in_c)
    return rows


def unit_rows(version):
    """{(unit, lib): [C functions, C bytes, asm functions, asm bytes]} over every config yaml."""
    cfg_dir = os.path.join(ROOT, "config", version)
    rows = {}
    for fn in sorted(os.listdir(cfg_dir)):
        if fn.endswith(".yaml") and fn != "bss.yaml":
            for k, v in segment_status(os.path.join(cfg_dir, fn)).items():
                acc = rows.setdefault(k, [0, 0, 0, 0])
                for i in range(4):
                    acc[i] += v[i]
    return rows


def totals(rows):
    """Sum unit_rows() per code kind: {"game": [...], "psyq": [...]}."""
    tot = {"game": [0, 0, 0, 0], "psyq": [0, 0, 0, 0]}
    for (_, lib), counts in rows.items():
        for i, v in enumerate(counts):
            tot[lib][i] += v
    return tot


def main():
    version = sys.argv[1] if len(sys.argv) > 1 else "us"
    rows = unit_rows(version)
    print("| Unidad | Código | Funciones en C | Bytes en C | Funciones en asm | Bytes en asm | % C (bytes) |")
    print("|---|---|---:|---:|---:|---:|---:|")
    for (unit, lib), (nc, bc, na, ba) in sorted(rows.items()):
        pct = 100.0 * bc / (bc + ba) if bc + ba else 0.0
        print(f"| {unit} | {lib} | {nc} | {bc} | {na} | {ba} | {pct:.2f} |")
    for lib, (nc, bc, na, ba) in totals(rows).items():
        pct = 100.0 * bc / (bc + ba) if bc + ba else 0.0
        print(f"| **total** | {lib} | {nc} | {bc} | {na} | {ba} | {pct:.2f} |")


if __name__ == "__main__":
    main()

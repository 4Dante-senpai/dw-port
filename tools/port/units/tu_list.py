#!/usr/bin/env python3
"""Generate the list of game translation units of a version and the unit each one links into.

The set of .c files comes from mk/version/<version>.mk (every <UNIT>_C_SRC list); the unit of
each file is the splat segment (config/<version>/<segment>.yaml) whose subsegments reference it.
Output: one TSV row per file, sorted: path, unit, status (`build` or `replaced`).

Usage: tu_list.py [-o FILE] [--root DIR] VERSION
"""
import argparse
import os
import pathlib
import re
import sys

import yaml

# CodeWarrior startup code (MIPS asm); the port replaces it with port/psyq/crt.c.
REPLACED = {"src/main/_psstart.c"}


def c_sources(mk_path):
    text = pathlib.Path(mk_path).read_text().replace("\\\n", " ")
    files = set()
    for m in re.finditer(r"^[A-Z0-9_]+_C_SRC\s*[:?+]?=(.*)$", text, re.M):
        files.update(w for w in m.group(1).split() if w.endswith(".c"))
    return files


def segment_refs(config_dir):
    """Map each referenced source path (without extension) to the set of segments using it."""
    refs = {}
    for name in sorted(os.listdir(config_dir)):
        if not name.endswith(".yaml"):
            continue
        cfg = yaml.safe_load(pathlib.Path(config_dir, name).read_text())
        src_path = (cfg.get("options") or {}).get("src_path")
        if not src_path:
            continue
        seg = cfg["name"]
        for segment in cfg.get("segments", []):
            if not isinstance(segment, dict):
                continue
            for sub in segment.get("subsegments", []):
                if isinstance(sub, list) and len(sub) >= 3 and isinstance(sub[2], str):
                    ref = os.path.normpath(os.path.join(src_path, sub[2]))
                    refs.setdefault(ref, set()).add(seg)
    return refs


def tu_list(root, version):
    files = c_sources(os.path.join(root, "mk", "version", version + ".mk"))
    refs = segment_refs(os.path.join(root, "config", version))
    rows, errors = [], []
    for f in sorted(files):
        segs = refs.get(f[:-2], set())
        if len(segs) != 1:
            errors.append(f"{f}: referenced by {len(segs)} segments {sorted(segs)}")
            continue
        rows.append((f, segs.pop(), "replaced" if f in REPLACED else "build"))
    return rows, errors


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("version")
    ap.add_argument("-o", "--output")
    ap.add_argument("--root", default=".")
    args = ap.parse_args()
    rows, errors = tu_list(args.root, args.version)
    if errors:
        sys.exit("tu_list: " + "\n".join(errors))
    out = "".join("\t".join(r) + "\n" for r in rows)
    if args.output:
        # Rewrite only on change so CMake does not rebuild needlessly.
        dst = pathlib.Path(args.output)
        if not dst.exists() or dst.read_text() != out:
            dst.write_text(out)
    else:
        sys.stdout.write(out)


if __name__ == "__main__":
    main()

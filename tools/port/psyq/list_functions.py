#!/usr/bin/env python3
"""Write tools/port/psyq/psyq_functions.tsv: name and library of every PsyQ function linked in
the game executable (the glabels of asm/<version>/main/psyq/<lib>.s). Names only, no code or data
(the same names are in config/<version>/symbols.txt). Needs `make regenerate` (class B).
Usage: list_functions.py [VERSION]
"""
import pathlib
import re
import sys

root = pathlib.Path(__file__).resolve().parents[3]
version = sys.argv[1] if len(sys.argv) > 1 else "us"
rows = []
for path in sorted((root / "asm" / version / "main" / "psyq").glob("*.s")):
    for m in re.finditer(r"^glabel (\S+)", path.read_text(), re.M):
        rows.append(f"{m.group(1)}\t{path.stem}")
out = root / "tools" / "port" / "psyq" / "psyq_functions.tsv"
out.write_text("name\tlib\n" + "\n".join(sorted(rows)) + "\n")
print(f"{len(rows)} functions -> {out.relative_to(root)}")

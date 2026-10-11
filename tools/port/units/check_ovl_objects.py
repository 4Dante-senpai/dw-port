#!/usr/bin/env python3
"""Checks that every overlay TU was compiled through its AD-6 wrapper.

For each TU of an overlay unit in the TU list, its object must exist under <objdir>/ovl/ and
hold no variables outside dw1ovl_<unit>: empty .data, .bss, .sdata, .sbss and no COMMON
symbols. String literals (.rodata.str*) are not variables and are allowed.
Usage: check_ovl_objects.py TU_LIST OBJDIR
"""
import pathlib
import re
import subprocess
import sys

tu_list, objdir = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
VAR_SECTION = re.compile(r"^\.(data|bss|sdata|sbss)(\.|$)")
errors, checked = [], 0
for row in tu_list.read_text().splitlines():
    path, unit, status = row.split("\t")
    if unit == "main" or status != "build":
        continue
    obj = objdir / "ovl" / (path + ".o")
    if not obj.exists():
        errors.append(f"{path}: no wrapped object {obj}")
        continue
    checked += 1
    out = subprocess.run(["readelf", "-SW", str(obj)], capture_output=True, text=True,
                         check=True).stdout
    for m in re.finditer(r"\]\s+(\S+)\s+\S+\s+\S+\s+\S+\s+([0-9a-f]+)", out):
        name, size = m.group(1), int(m.group(2), 16)
        if size and VAR_SECTION.match(name):
            errors.append(f"{path}: {size} bytes in {name}, outside dw1ovl_{unit}")
    syms = subprocess.run(["nm", str(obj)], capture_output=True, text=True, check=True).stdout
    for line in syms.splitlines():
        if line.split()[-2:-1] == ["C"]:
            errors.append(f"{path}: COMMON symbol {line.split()[-1]}")
if errors or not checked:
    sys.exit("check_ovl_objects:\n" + "\n".join(errors or ["no overlay TU checked"]))
print(f"check_ovl_objects: {checked} overlay TUs, all variables in their dw1ovl_ section")

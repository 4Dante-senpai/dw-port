#!/usr/bin/env python3
"""List the game TUs whose object code depends on the signedness of plain `char`.

Recompiles every game TU of the native-headless build (from its compile_commands.json) with
-fsigned-char (the build flag) and with -funsigned-char, without debug info, and compares the
objects byte for byte (any section). The forced port_game.h is left out of both variants.
Usage (in the port image, after configuring the preset): char_sign_diff.py [BUILD_DIR]
"""
import json
import pathlib
import shlex
import subprocess
import sys
import tempfile

root = pathlib.Path(__file__).resolve().parents[3]
build = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else root / "build/port/native-headless")
commands = json.loads((build / "compile_commands.json").read_text())
checked, differ = 0, []
with tempfile.TemporaryDirectory() as tmp:
    for entry in sorted(commands, key=lambda e: e["file"]):
        args = shlex.split(entry["command"])
        if "-fsigned-char" not in args:
            continue
        # Drop the forced port_game.h: its signed-char assert would stop the unsigned variant.
        i = args.index("-include")
        del args[i:i + 2]
        checked += 1
        objs = []
        for sign in ("-fsigned-char", "-funsigned-char"):
            out = pathlib.Path(tmp, f"{len(objs)}.o")
            a = [sign if x == "-fsigned-char" else x for x in args if x != "-g"]
            a[a.index("-o") + 1] = str(out)
            subprocess.run(a, cwd=entry["directory"], check=True)
            objs.append(out.read_bytes())
        if objs[0] != objs[1]:
            differ.append(str(pathlib.Path(entry["file"]).resolve().relative_to(root)))
if not checked:
    sys.exit("char_sign_diff: no game TU is compiled with -fsigned-char")
print(f"tus_checked={checked} tus_differ={len(differ)}")
print("\n".join(differ))

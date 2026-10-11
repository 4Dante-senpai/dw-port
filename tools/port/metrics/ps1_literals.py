#!/usr/bin/env python3
"""Inventory of PS1 RAM address literals (0x80000000-0x801FFFFF) in src/ and include/ (AD-4).

Writes tools/port/metrics/ps1_literals.tsv, keeping the status of existing rows.
Status: `traducido`, `no aplica: <motivo>` or `pendiente`.
Usage: ps1_literals.py [--check] [--no-pending]
"""
import pathlib
import re
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import inventory  # noqa: E402

LITERAL = re.compile(r"\b0[xX]80[01][0-9A-Fa-f]{5}(?:[uU]?[lL]{0,2}|[lL]{1,2}[uU])?\b")


def findings():
    for f in inventory.game_files():
        text = inventory.read(f)
        lines = text.split("\n")
        stripped = inventory.strip_c(text).split("\n")
        for n, code in enumerate(stripped, 1):
            for m in LITERAL.finditer(code):
                value = int(m.group().rstrip("uUlL"), 16)
                if "_bss_" in f:
                    default = "no aplica: residuo del REL"
                elif value == 0x80000000:
                    default = "no aplica: constante"
                else:
                    default = inventory.PENDING
                yield f, m.group().lower().rstrip("ul"), inventory.norm(lines[n - 1]), n, default


if __name__ == "__main__":
    inventory.main("ps1_literals", findings)

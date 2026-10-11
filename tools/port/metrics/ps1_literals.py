#!/usr/bin/env python3
"""Inventory of PS1 RAM address literals (0x80000000-0x801FFFFF) in src/ and include/ (AD-4).

Writes tools/port/metrics/ps1_literals.tsv, keeping the status of existing rows.
Status: `traducido`, `no aplica: <motivo>` or `pendiente`. Inside a `#ifdef DW1_PORT` block the
status is set automatically: `traducido` in the port branch when the literal is the argument of
PS1_RAM_ADDR, `traducido: rama PS1` in its `#else` (the untouched PS1 code). Every DW1_PORT
block must translate exactly the literals of its PS1 branch, in order, or the script fails.
Usage: ps1_literals.py [--check] [--no-pending]
"""
import pathlib
import re
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import inventory  # noqa: E402

LITERAL = re.compile(r"\b0[xX]80[01][0-9A-Fa-f]{5}(?:[uU]?[lL]{0,2}|[lL]{1,2}[uU])?\b")
CONDITIONAL = re.compile(r"^\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b\s*(.*)$")


def port_branches(lines):
    """Per line: 'port' in the DW1_PORT branch of a block, 'ps1' in its other branch, '' elsewhere.

    Recognizes `#ifdef DW1_PORT`, `#if defined(DW1_PORT)` and `#ifndef DW1_PORT`; an `#elif`
    inside such a block makes the rest of it 'other'. Expects comment-free lines (strip_c).
    """
    stack, out = [], []
    for line in lines:
        m = CONDITIONAL.match(line)
        d, arg = (m.group(1), m.group(2).strip()) if m else ("", "")
        if d in ("if", "ifdef", "ifndef"):
            if (d == "ifdef" and arg == "DW1_PORT") or (d == "if" and arg in ("defined(DW1_PORT)", "defined DW1_PORT")):
                stack.append("port")
            elif d == "ifndef" and arg == "DW1_PORT":
                stack.append("ps1")
            else:
                stack.append("other")
        elif d == "elif" and stack:
            stack[-1] = "other"
        elif d == "else" and stack:
            stack[-1] = {"port": "ps1", "ps1": "port"}.get(stack[-1], stack[-1])
        elif d == "endif" and stack:
            stack.pop()
        if d:
            out.append("")
            continue
        out.append(next((s for s in reversed(stack) if s != "other"), ""))
    return out


def block_errors(f, lines, branch):
    """DW1_PORT blocks whose port branch does not translate the PS1 branch's literals in order."""
    errors, port, ps1, start = [], [], [], None
    for n, (code, b) in enumerate(zip(lines + [""], branch + [""]), 1):
        if b and start is None:
            start = n
        if b == "port":
            port += [lit.lower() for lit in LITERAL.findall(code)]
        elif b == "ps1":
            ps1 += [lit.lower() for lit in LITERAL.findall(code)]
        elif start is not None and not b and not CONDITIONAL.match(code):
            if port != ps1:
                errors.append(f"{f}:{start}: DW1_PORT branch literals {port} != PS1 branch {ps1}")
            port, ps1, start = [], [], None
    return errors


def scan(files=None, root=inventory.ROOT):
    rows, errors = [], []
    for f in inventory.game_files() if files is None else files:
        text = inventory.read(f, root)
        lines = text.split("\n")
        stripped = inventory.strip_c(text).split("\n")
        branch = port_branches(stripped)
        errors += block_errors(f, stripped, branch)
        for n, code in enumerate(stripped, 1):
            for m in LITERAL.finditer(code):
                value = int(m.group().rstrip("uUlL"), 16)
                if branch[n - 1] == "port" and code[:m.start()].rstrip().endswith("PS1_RAM_ADDR("):
                    default = "traducido"
                elif branch[n - 1] == "ps1":
                    default = "traducido: rama PS1"
                elif "_bss_" in f:
                    default = "no aplica: residuo del REL"
                elif value == 0x80000000:
                    default = "no aplica: constante"
                else:
                    default = inventory.PENDING
                rows.append((f, m.group().lower().rstrip("ul"), inventory.norm(lines[n - 1]), n,
                             default))
    return rows, errors


def findings(files=None, root=inventory.ROOT):
    rows, errors = scan(files, root)
    if errors:
        sys.exit("ps1_literals:\n" + "\n".join(errors))
    return rows


if __name__ == "__main__":
    inventory.main("ps1_literals", findings)

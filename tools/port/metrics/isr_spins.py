#!/usr/bin/env python3
"""Inventory of game loops that make no call at all (AD-8).

On the PS1 such a loop can wait for a value written by an interrupt callback; in the port no
time passes inside it, so each one is classified by hand: `port_yield` (it waits for a
callback or the hardware) or `no aplica: <motivo>` (it progresses by itself).
Candidates: `while`/`do` loops, and `for` loops with an empty increment, whose condition and
body contain no call. Writes tools/port/metrics/isr_spins.tsv keeping existing statuses.
Usage: isr_spins.py [--check] [--no-pending]

The kind column is `<loop>@<enclosing function>`, so a row keeps its status when other code of
the file moves.

ponytail: textual scan, with known blind spots that show up as hangs in the L2 Scenes:
- a loop that calls game code which never reaches PsyQ;
- a loop whose only "call" is a function-like macro (counted as a call);
- loops built with goto, and `for` loops with an increment.
Add a call-graph / AST pass if any of these happens more than once.
"""
import pathlib
import re
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import inventory  # noqa: E402

KEYWORDS = {"if", "while", "for", "switch", "sizeof", "return", "do", "else", "case"}
CALL = re.compile(r"\b([A-Za-z_]\w*)\s*\(")
INDEXED_CALL = re.compile(r"\]\s*\(")
LOOP = re.compile(r"\b(while|for|do)\b")


def close(text, i, op, cl):
    depth = 0
    for j in range(i, len(text)):
        if text[j] == op:
            depth += 1
        elif text[j] == cl:
            depth -= 1
            if depth == 0:
                return j
    return len(text) - 1


def statement(text, i):
    """End offset (inclusive) of the statement starting at or after i."""
    k = i + len(text[i:]) - len(text[i:].lstrip())
    if text[k:k + 1] == "{":
        return close(text, k, "{", "}")
    return text.find(";", k)


def has_call(code):
    # Named calls, plus calls through an array element such as `jtbl[op]()`.
    return any(name not in KEYWORDS for name in CALL.findall(code)) or bool(INDEXED_CALL.search(code))


def loops(text):
    """(offset, kind, condition, code) for each loop; do-while tails are not loops of their own."""
    tails = set()
    for m in LOOP.finditer(text):
        kind, i = m.group(1), m.end()
        if kind == "do":
            end = statement(text, i)
            w = re.match(r"\s*while\s*\(", text[end + 1:])
            if not w:
                continue
            cs = end + w.end()
            ce = close(text, cs, "(", ")")
            tails.add(end + 1 + len(text[end + 1:]) - len(text[end + 1:].lstrip()))
            yield m.start(), kind, cs, text[cs:ce + 1], text[i:ce + 1]
            continue
        if m.start() in tails:
            continue
        p = re.match(r"\s*\(", text[i:])
        if not p:
            continue
        cs = i + p.end() - 1
        ce = close(text, cs, "(", ")")
        header = text[cs:ce + 1]
        if kind == "for" and header[1:-1].split(";")[-1].strip():
            continue
        yield m.start(), kind, cs, header, text[cs:statement(text, ce + 1) + 1]


def findings():
    for f in inventory.game_files():
        source = inventory.read(f)
        text = inventory.strip_c(source)
        for offset, kind, cs, cond, code in loops(text):
            if not has_call(code):
                # strip_c keeps offsets, so the condition is reported as written.
                line = text.count("\n", 0, offset) + 1
                kind = f"{kind}@{inventory.enclosing_function(text, offset)}"
                yield (f, kind, inventory.norm(source[cs:cs + len(cond)]), line,
                       inventory.PENDING)


if __name__ == "__main__":
    inventory.main("isr_spins", findings)

"""Shared code of the source inventories (ps1_literals.py, isr_spins.py).

An inventory is a versioned TSV with one row per finding: key columns, the line, and a status
assigned by hand. Regenerating keeps the status of every row whose key still exists.
"""
import argparse
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools" / "port" / "units"))
import tu_list  # noqa: E402

HEADER = ["file", "kind", "text", "ordinal", "line", "status"]
PENDING = "pendiente"


def game_files(root=ROOT):
    """US game TUs plus every header under include/, as repo-relative paths."""
    rows, errors = tu_list.tu_list(str(root), "us")
    if errors:
        sys.exit("inventory: " + "\n".join(errors))
    headers = sorted(str(p.relative_to(root)) for p in (root / "include").rglob("*.h"))
    return [r[0] for r in rows] + headers


def strip_c(text):
    """Blank out comments and string/char literals, keeping every newline and offset."""
    out, i, n = [], 0, len(text)
    while i < n:
        if text.startswith("//", i):
            j = text.find("\n", i)
            j = n if j < 0 else j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
        elif text[i] in "\"'":
            q, j = text[i], i + 1
            while j < n and text[j] != q and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            if j >= n or text[j] == "\n":
                # Unterminated (e.g. an apostrophe under #if 0): keep the newline in place.
                out.append(" " * (j - i))
                i = j
                continue
            out.append(q + re.sub(r"[^\n]", " ", text[i + 1:j]) + q)
            i = j + 1
            continue
        else:
            out.append(text[i])
            i += 1
            continue
        out.append(re.sub(r"[^\n]", " ", text[i:j]))
        i = j
    return "".join(out)


def read(path, root=ROOT):
    return (root / path).read_text(encoding="latin-1")


def enclosing_function(text, offset):
    """Name of the function whose body contains offset ('' at file scope)."""
    depth, start = 0, None
    for m in re.finditer(r"[{}]", text[:offset]):
        if m.group() == "{":
            if depth == 0:
                start = m.start()
            depth += 1
        else:
            depth -= 1
    if depth <= 0 or start is None:
        return ""
    # The function name precedes the last parameter list before the body (K&R declarations
    # between the list and the brace end in `;`, never in `)`).
    close = text.rfind(")", 0, start)
    level = 0
    for j in range(close, -1, -1):
        level += {")": 1, "(": -1}.get(text[j], 0)
        if level == 0:
            m = re.search(r"([A-Za-z_]\w*)\s*$", text[:j])
            return m.group(1) if m else ""
    return ""


def norm(code):
    return " ".join(code.split())


def number(findings):
    """findings: (file, kind, text, line, default_status) -> rows with an ordinal per same key."""
    seen, rows = {}, []
    for f, kind, text, line, default in findings:
        k = (f, kind, text)
        seen[k] = seen.get(k, 0) + 1
        rows.append([f, kind, text, str(seen[k]), str(line), default])
    return rows


def render(rows):
    return "".join("\t".join(r) + "\n" for r in [HEADER] + rows)


def load(path):
    if not path.exists():
        return {}
    lines = path.read_text(encoding="utf-8").split("\n")[1:-1]
    return {tuple(c[:4]): c[5] for c in (line.split("\t") for line in lines)}


def merge(rows, old):
    """Keep the status of rows already in the versioned TSV (same key).

    Returns the rows and the old rows with a hand-set status whose key no longer exists, so a
    regeneration never drops a classification silently.
    """
    for r in rows:
        # A pending row takes the freshly computed default (it may now be classified).
        kept = old.get(tuple(r[:4]), PENDING)
        if kept != PENDING:
            r[5] = kept
    keys = {tuple(r[:4]) for r in rows}
    lost = [(k, s) for k, s in old.items() if k not in keys and s != PENDING]
    return rows, lost


def main(name, findings_fn):
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true", help="fail if the versioned TSV is stale")
    ap.add_argument("--no-pending", action="store_true", help="also fail on pending rows")
    ap.add_argument("--tsv", type=pathlib.Path, help="inventory file (default: the versioned one)")
    args = ap.parse_args()
    tsv = args.tsv or ROOT / "tools" / "port" / "metrics" / f"{name}.tsv"
    rows, lost = merge(number(findings_fn()), load(tsv))
    out = render(rows)
    for key, status in lost:
        print(f"{name}: status dropped, its row no longer exists: {key} = {status}")
    if args.check:
        if not tsv.exists() or tsv.read_text(encoding="utf-8") != out:
            sys.exit(f"{name}: {tsv} is stale; regenerate with "
                     f"tools/port/metrics/{name}.py and set the status of new rows")
    else:
        tsv.write_text(out, encoding="utf-8")
    pending = [r for r in rows if r[5] == PENDING]
    print(f"{name}: rows={len(rows)} pending={len(pending)}")
    if args.no_pending and pending:
        sys.exit("\n".join(f"{r[0]}:{r[4]}: {r[2]}" for r in pending))

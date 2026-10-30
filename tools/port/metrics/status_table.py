#!/usr/bin/env python3
"""Project metrics for docs/STATUS.md, computed from the repository (story 8.1).

Usage: tools/port/metrics/status_table.py [--root DIR] [--junit FILE]... [--update FILE]

Prints a Markdown table with:
- game functions in C / total (tools/port/metrics/decomp_status.py; needs asm/us/ from
  `make regenerate`);
- PsyQ functions implemented / used: a used function (docs/research/psyq-usage.md) counts when the
  port defines it in C and the comment right before the definition names its source (`dw2-pc:`,
  `asm:` or `stub-m2:`);
- DW1_PORT blocks in src/ and include/: #if/#ifdef/#ifndef/#elif lines naming DW1_PORT;
- rows of docs/port/src-changes.md: table rows whose `Archivo` cell is a src/ or include/ path;
- passing Scenes (tools/port/scenes/*.toml) from `ctest --output-junit` files: native when
  scene_<s>_compare or scene_<s>_state passed, web when web_scene_<s> passed.
A metric that cannot be computed here reads "sin verificar (<reason>)".

--update rewrites the lines between STATUS_BEGIN and STATUS_END of FILE with the table.
Exit status: 0 OK; 1 when a file's DW1_PORT blocks differ from its src-changes.md rows (NFR-10),
listed on stderr, and FILE is not written; 2 on unreadable inputs or missing markers.
"""
import argparse
import collections
import glob
import os
import re
import sys
import tempfile
import xml.etree.ElementTree as ET

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
STATUS_BEGIN = "<!-- status_table:begin -->"
STATUS_END = "<!-- status_table:end -->"
MARKERS = ("dw2-pc", "asm", "stub-m2")
GUARD = re.compile(r"^\s*#\s*(?:if|ifdef|ifndef|elif)\b.*\bDW1_PORT\b")
USED = re.compile(r"^\| `(\w+)` \|", re.M)
COMMENT_AT_END = re.compile(r"/\*(?:(?!\*/).)*\*/\s*$", re.S)
MARKER = re.compile(r"(?<![\w-])(" + "|".join(map(re.escape, MARKERS)) + r"):")
SOURCE_PATH = re.compile(r"`((?:src|include)/[^`]+)`")


class InputError(Exception):
    pass


def unverified(reason):
    return f"sin verificar ({reason})"


def game_functions(root, version="us"):
    if not os.path.isdir(os.path.join(root, "asm", version)):
        return unverified(f"falta `asm/{version}/`; correr `make regenerate`")
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    try:
        import decomp_status  # needs PyYAML only when asm/ exists
    except ImportError as e:
        return unverified(f"no se pudo importar decomp_status: {e}")
    decomp_status.ROOT = root
    try:
        nc, _, na, _ = decomp_status.totals(decomp_status.unit_rows(version))["game"]
    except OSError as e:
        return unverified(f"salida de splat incompleta: {e}")
    return f"{nc} / {nc + na}"


def psyq_used(root):
    with open(os.path.join(root, "docs/research/psyq-usage.md"), encoding="utf-8") as f:
        return list(dict.fromkeys(USED.findall(f.read())))


def port_sources(root):
    paths = sorted(glob.glob(os.path.join(root, "port", "**", "*.c"), recursive=True))
    sources = {}
    for path in paths:
        if "tests" in os.path.relpath(path, root).split(os.sep):
            continue
        with open(path, encoding="utf-8", errors="replace") as f:
            sources[path] = f.read()
    return sources


def psyq_marker(sources, name):
    """Marker of the comment right before name's C definition, '' when unmarked, None if undefined."""
    definition = re.compile(r"^(?:[A-Za-z_][\w \t*]*?[\s*])?" + re.escape(name)
                            + r"\s*\([^;{}]*\)\s*\{", re.M)
    for text in sources.values():
        m = definition.search(text)
        if m:
            comment = COMMENT_AT_END.search(text, 0, m.start())
            found = MARKER.search(comment.group(0)) if comment else None
            return found.group(1) if found else ""
    return None


def psyq_functions(root):
    used = psyq_used(root)
    sources = port_sources(root)
    kinds = collections.Counter()
    unmarked = []
    for name in used:
        marker = psyq_marker(sources, name)
        if marker:
            kinds[marker] += 1
        elif marker == "":
            unmarked.append(name)
    detail = ", ".join(f"`{k}:` {kinds[k]}" for k in MARKERS)
    value = f"{sum(kinds.values())} / {len(used)} ({detail})"
    return value, unmarked


def port_guards(root):
    counts = collections.Counter()
    for top in ("src", "include"):
        for path in sorted(glob.glob(os.path.join(root, top, "**", "*"), recursive=True)):
            if not os.path.isfile(path):
                continue
            with open(path, encoding="utf-8", errors="replace") as f:
                n = sum(1 for line in f if GUARD.match(line))
            if n:
                counts[os.path.relpath(path, root).replace(os.sep, "/")] += n
    return counts


def cells(line):
    return [c.strip() for c in line.strip().strip("|").split("|")]


def change_rows(root):
    """Rows per file of the Markdown tables in src-changes.md that have an `Archivo` column."""
    counts = collections.Counter()
    column = None
    with open(os.path.join(root, "docs/port/src-changes.md"), encoding="utf-8") as f:
        lines = f.read().splitlines()
    for i, line in enumerate(lines):
        if not line.startswith("|"):
            column = None
            continue
        row = cells(line)
        if column is None:
            is_header = i + 1 < len(lines) and re.fullmatch(r"\|[\s:|-]+\|?", lines[i + 1].strip())
            column = row.index("Archivo") if is_header and "Archivo" in row else -1
            continue
        if column < 0 or re.fullmatch(r"[\s:-]*", "".join(row)):
            continue
        m = SOURCE_PATH.fullmatch(row[column]) if column < len(row) else None
        if m:
            counts[m.group(1)] += 1
    return counts


def junit_results(paths):
    """{test name: status} where status is passed, failed or skipped; across files the worst wins
    (failed over passed over skipped), since the native presets register the same tests."""
    rank = {"skipped": 0, "passed": 1, "failed": 2}
    results = {}
    for path in paths:
        try:
            tree = ET.parse(path)
        except (OSError, ET.ParseError) as e:
            raise InputError(f"JUnit ilegible: {path}: {e}")
        for case in tree.iter("testcase"):
            status = case.get("status", "")
            if case.find("failure") is not None or case.find("error") is not None or status == "fail":
                result = "failed"
            elif case.find("skipped") is not None or status in ("notrun", "disabled"):
                result = "skipped"
            else:
                result = "passed"
            name = case.get("name")
            if rank[result] >= rank[results.get(name, "skipped")]:
                results[name] = result
    return results


def scenes(root):
    paths = glob.glob(os.path.join(root, "tools/port/scenes/*.toml"))
    return sorted(os.path.splitext(os.path.basename(p))[0] for p in paths)


def passing_scenes(root, junit_paths):
    names = scenes(root)
    if not junit_paths:
        reason = unverified("sin resultados de CTest; pasar `--junit`")
        return reason, reason
    results = junit_results(junit_paths)

    def count(tests_of):
        """A Scene passes when one of its gate tests passed and none failed."""
        ok = [s for s in names
              if any(results.get(t) == "passed" for t in tests_of(s))
              and not any(results.get(t) == "failed" for t in tests_of(s))]
        return f"{len(ok)} / {len(names)}" + (f" ({', '.join(ok)})" if ok else "")
    native = count(lambda s: (f"scene_{s}_compare", f"scene_{s}_state"))
    web = count(lambda s: (f"web_scene_{s}",))
    return native, web


def nfr10_mismatches(guards, rows):
    return [(path, guards[path], rows[path]) for path in sorted(set(guards) | set(rows))
            if guards[path] != rows[path]]


def table(root, junit_paths):
    guards = port_guards(root)
    rows = change_rows(root)
    psyq, unmarked = psyq_functions(root)
    native, web = passing_scenes(root, junit_paths)
    lines = [
        "| Métrica | Valor |",
        "|---|---|",
        f"| Funciones del Juego en C / total (USA) | {game_functions(root)} |",
        f"| Funciones PsyQ implementadas / usadas (por comentario de fuente) | {psyq} |",
        f"| Bloques `DW1_PORT` en `src/` e `include/` | {sum(guards.values())} |",
        f"| Filas de `docs/port/src-changes.md` | {sum(rows.values())} |",
        f"| Escenas que pasan (nativo) | {native} |",
        f"| Escenas que pasan (web) | {web} |",
    ]
    return lines, nfr10_mismatches(guards, rows), unmarked


def update(path, lines):
    with open(path, encoding="utf-8") as f:
        text = f.read()
    begin = text.find(STATUS_BEGIN)
    end = text.find(STATUS_END, begin) if begin >= 0 else -1
    if end < 0:
        raise InputError(f"{path}: faltan los marcadores {STATUS_BEGIN} / {STATUS_END}")
    head = text[:begin + len(STATUS_BEGIN)]
    fd, tmp = tempfile.mkstemp(dir=os.path.dirname(os.path.abspath(path)), suffix=".tmp")
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as f:
            f.write(head + "\n" + "\n".join(lines) + "\n" + text[end:])
        os.chmod(tmp, os.stat(path).st_mode & 0o7777)
        os.replace(tmp, path)
    except BaseException:
        os.unlink(tmp)
        raise


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--root", default=ROOT)
    parser.add_argument("--junit", action="append", default=[])
    parser.add_argument("--update")
    args = parser.parse_args(argv)
    try:
        lines, mismatches, unmarked = table(args.root, args.junit)
        print("\n".join(lines))
        for name in unmarked:
            print(f"status_table: PsyQ function without a source comment: {name}", file=sys.stderr)
        if mismatches:
            for path, guards, rows in mismatches:
                print(f"status_table: NFR-10: {path}: {guards} DW1_PORT blocks, {rows} rows in "
                      "docs/port/src-changes.md", file=sys.stderr)
            return 1
        if args.update:
            update(args.update, lines)
    except (InputError, OSError) as e:
        print(f"status_table: {e}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())

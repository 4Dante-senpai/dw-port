#!/usr/bin/env python3
"""Synthetic repositories for status_table.py; no disc data, no asm/ needed."""
import contextlib
import importlib.util
import io
import os
import sys
import tempfile
import textwrap
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
spec = importlib.util.spec_from_file_location('status_table', Path(__file__).with_name('status_table.py'))
status_table = importlib.util.module_from_spec(spec)
spec.loader.exec_module(status_table)

PSYQ_USAGE = """\
### libcd

| Función | Llamadas | Ref. dirección | Unidades que la usan | En DW2-PC |
|---|---:|---:|---|---|
| `CdRead` | 3 | 0 | main | sí |
| `CdSync` | 1 | 0 | main | no |

### libc

| Función | Llamadas | Ref. dirección | Unidades que la usan | En DW2-PC |
|---|---:|---:|---|---|
| `memcpy` | 52 | 0 | main | no |
| `SsInit` | 1 | 0 | main | no |
| `DrawOTag` | 1 | 0 | main | sí |
"""

PORT_C = """\
/* asm:asm/us/main/psyq/libcd.s CdRead */
int CdRead(int n, unsigned long *buf, int mode) { return n; }

/* dw2-pc:psyq/libgpu.c DrawOTag */
void
DrawOTag(unsigned long *ot)
{
}

/* stub-m2: SsInit does nothing yet. */
void SsInit(void) { }

/* A helper without a source. */
int CdSync(int mode, unsigned char *result)
{
	return 0;
}
"""

SRC_C = """\
#ifdef DW1_PORT
#include "clock.h"
#endif
void f(void)
{
#if defined(DW1_PORT) && !defined(DW1_ABI_AUDIT)
	g(1);
#elif DW1_PORT_OTHER
	g(2);
#else
	g(3);
#endif
}
"""

CHANGES = """\
# Cambios

| Fecha | Archivo | Función / símbolo | Motivo | Historia | Verificación |
|---|---|---|---|---|---|
| 2026-10-11 | `src/main/a.c` | include | x | 2.4 | L0a |
| 2026-10-11 | `config/us/main.yaml` | no cuenta | x | 2.4 | L0a |

Texto con `src/main/a.c` fuera de una tabla.

| Archivo | Bloque / símbolo | Motivo | Verificación |
|---|---|---|---|
| `src/main/a.c` | `f` | x | L0a |
| `include/b.h` | `B` | x | L0a |

| Medida | Archivo de referencia |
|---|---|
| no | sí |

| Nombre | Valor |
|---|---|
| `src/main/a.c` | sin columna Archivo |
"""

JUNIT = """\
<testsuite name="Linux" tests="6">
  <testcase name="scene_title_compare" status="run"/>
  <testcase name="scene_newgame_state" status="run"/>
  <testcase name="scene_movie0_compare" status="fail"><failure message="Failed"/></testcase>
  <testcase name="scene_naming_compare" status="notrun"><skipped message="Skipped"/></testcase>
  <testcase name="web_scene_title" status="run"/>
  <testcase name="scene_title" status="run"/>
</testsuite>
"""


def write(root, rel, text):
    path = Path(root, rel)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(textwrap.dedent(text))
    return path


def make_repo(root):
    write(root, "docs/research/psyq-usage.md", PSYQ_USAGE)
    write(root, "port/psyq/libcd.c", PORT_C)
    write(root, "port/psyq/tests/test_libc.c", "/* asm: memcpy */\nvoid *memcpy(void *d) { return d; }\n")
    write(root, "src/main/a.c", SRC_C)
    write(root, "include/b.h", "#ifndef DW1_PORT\n#define B 1\n#endif\n")
    write(root, "include/c.inc", "# if 0 /* DW1_PORTABLE is not the guard */\n# endif\n")
    write(root, "docs/port/src-changes.md", CHANGES)
    for name in ("title", "newgame", "movie0", "naming"):
        write(root, f"tools/port/scenes/{name}.toml", "")


def make_asm(root):
    write(root, "config/us/main.yaml", """\
        options:
          asm_path: asm/us
          src_path: src
        segments:
          - type: code
            subsegments:
              - [0x0, c, main/foo]
              - [0x100, asm, main/bar]
              - [0x180, asm, psyq/libcd]
          - [0x200, bin]
        """)
    write(root, "config/us/bss.yaml", "ignored: true\n")
    write(root, "asm/us/matchings/main/foo/foo.s", "nonmatching foo, 0x10\n")
    write(root, "asm/us/matchings/main/foo/baz.s", "nonmatching baz, 0x8\n")
    write(root, "src/main/foo.c", "void foo(void)\n{\n}\n")
    write(root, "asm/us/main/bar.s", "nonmatching bar, 0x20\n")
    write(root, "asm/us/psyq/libcd.s", "nonmatching CdRead, 0x40\nnonmatching CdSync, 0x4\n")


def run(*argv):
    out, err = io.StringIO(), io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
        code = status_table.main(list(argv))
    return code, out.getvalue(), err.getvalue()


def metric(out, name):
    for line in out.splitlines():
        if line.startswith(f"| {name} |"):
            return line.split(" | ", 1)[1].rstrip(" |")
    raise AssertionError(f"missing metric {name}: {out}")


class StatusTableTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = self.tmp.name
        make_repo(self.root)

    def tearDown(self):
        self.tmp.cleanup()

    def test_consistent_repo_prints_every_metric(self):
        code, out, err = run("--root", self.root)
        self.assertEqual(code, 0, err)
        self.assertEqual(metric(out, "Bloques `DW1_PORT` en `src/` e `include/`"), "3")
        self.assertEqual(metric(out, "Filas de `docs/port/src-changes.md`"), "3")
        self.assertEqual(len([line for line in out.splitlines() if line.startswith("| ")]), 7)

    def test_guards_count_if_variants_only(self):
        write(self.root, "src/main/a.c", SRC_C + "#  ifdef DW1_PORT\n#endif\n#if X\n#elif defined(DW1_PORT)\n#endif\n")
        self.assertEqual(status_table.port_guards(self.root),
                         {"src/main/a.c": 4, "include/b.h": 1})

    def test_missing_row_fails_with_counts(self):
        write(self.root, "src/main/d.c", "#ifdef DW1_PORT\n#endif\n#ifdef DW1_PORT\n#endif\n")
        code, out, err = run("--root", self.root)
        self.assertEqual(code, 1)
        self.assertIn("src/main/d.c: 2 DW1_PORT blocks, 0 rows", err)
        self.assertIn("| Métrica | Valor |", out)

    def test_extra_row_fails(self):
        write(self.root, "docs/port/src-changes.md",
              CHANGES + "\n| Archivo | Motivo |\n|---|---|\n| `src/main/gone.c` | x |\n")
        code, _, err = run("--root", self.root)
        self.assertEqual(code, 1)
        self.assertIn("src/main/gone.c: 0 DW1_PORT blocks, 1 rows", err)

    def test_psyq_markers_and_unmarked_functions(self):
        code, out, err = run("--root", self.root)
        self.assertEqual(code, 0)
        self.assertEqual(metric(out, "Funciones PsyQ implementadas / usadas (por comentario de fuente)"),
                         "3 / 5 (`dw2-pc:` 1, `asm:` 1, `stub-m2:` 1)")
        self.assertIn("without a source comment: CdSync", err)
        self.assertNotIn("memcpy", err)

    def test_game_functions_without_asm_are_unverified(self):
        _, out, _ = run("--root", self.root)
        self.assertEqual(metric(out, "Funciones del Juego en C / total (USA)"),
                         "sin verificar (falta `asm/us/`; correr `make regenerate`)")

    def test_game_functions_from_splat_output(self):
        make_asm(self.root)
        code, out, err = run("--root", self.root)
        self.assertEqual(code, 0, err)
        self.assertEqual(metric(out, "Funciones del Juego en C / total (USA)"), "1 / 3")
        self.assertEqual(status_table.sys.modules["decomp_status"].totals(
            status_table.sys.modules["decomp_status"].unit_rows("us"))["psyq"], [0, 0, 2, 0x44])

    def test_partial_splat_output_is_unverified(self):
        make_asm(self.root)
        os.remove(os.path.join(self.root, "asm/us/main/bar.s"))
        code, out, _ = run("--root", self.root)
        self.assertEqual(code, 0)
        self.assertTrue(metric(out, "Funciones del Juego en C / total (USA)").startswith(
            "sin verificar (salida de splat incompleta:"))

    def test_scenes_without_junit_are_unverified(self):
        _, out, _ = run("--root", self.root)
        for kind in ("nativo", "web"):
            self.assertEqual(metric(out, f"Escenas que pasan ({kind})"),
                             "sin verificar (sin resultados de CTest; pasar `--junit`)")

    def test_scenes_from_junit(self):
        junit = write(self.root, "build/ctest.xml", JUNIT)
        code, out, err = run("--root", self.root, "--junit", str(junit))
        self.assertEqual(code, 0, err)
        self.assertEqual(metric(out, "Escenas que pasan (nativo)"), "2 / 4 (newgame, title)")
        self.assertEqual(metric(out, "Escenas que pasan (web)"), "1 / 4 (title)")

    def test_junit_files_merge_to_the_worst_status(self):
        first = write(self.root, "build/a.xml", JUNIT)
        second = write(self.root, "build/b.xml", """\
            <testsuite>
              <testcase name="scene_title_compare" status="fail"><failure message="Failed"/></testcase>
              <testcase name="scene_newgame_state" status="notrun"><skipped message="Skipped"/></testcase>
              <testcase name="scene_naming_compare" status="run"/>
            </testsuite>
            """)
        for order in ((first, second), (second, first)):
            with self.subTest(order=order):
                _, out, _ = run("--root", self.root, "--junit", str(order[0]), "--junit", str(order[1]))
                self.assertEqual(metric(out, "Escenas que pasan (nativo)"), "2 / 4 (naming, newgame)")

    def test_scene_with_a_failed_gate_does_not_pass(self):
        junit = write(self.root, "build/c.xml", """\
            <testsuite>
              <testcase name="scene_title_state" status="run"/>
              <testcase name="scene_title_compare" status="fail"><failure message="Failed"/></testcase>
            </testsuite>
            """)
        _, out, _ = run("--root", self.root, "--junit", str(junit))
        self.assertEqual(metric(out, "Escenas que pasan (nativo)"), "0 / 4")

    def test_missing_input_exits_2(self):
        os.remove(os.path.join(self.root, "docs/port/src-changes.md"))
        code, _, err = run("--root", self.root)
        self.assertEqual(code, 2)
        self.assertIn("src-changes.md", err)

    def test_unreadable_junit_exits_2(self):
        bad = write(self.root, "build/bad.xml", "<testsuite>")
        self.assertEqual(run("--root", self.root, "--junit", str(bad))[0], 2)
        self.assertEqual(run("--root", self.root, "--junit", os.path.join(self.root, "nope.xml"))[0], 2)

    def test_update_replaces_only_the_marked_block(self):
        status = write(self.root, "STATUS.md", "# S\n\n<!-- status_table:begin -->\nold\n"
                       "<!-- status_table:end -->\n\nresto\n")
        code, out, _ = run("--root", self.root, "--update", str(status))
        self.assertEqual(code, 0)
        self.assertEqual(status.read_text(), "# S\n\n<!-- status_table:begin -->\n" + out
                         + "<!-- status_table:end -->\n\nresto\n")

    def test_update_keeps_mode_and_skips_a_stray_end_marker(self):
        status = write(self.root, "STATUS.md", "<!-- status_table:end -->\n<!-- status_table:begin -->\n"
                       "old\n<!-- status_table:end -->\n")
        os.chmod(status, 0o644)
        code, out, _ = run("--root", self.root, "--update", str(status))
        self.assertEqual(code, 0)
        self.assertEqual(status.read_text(), "<!-- status_table:end -->\n<!-- status_table:begin -->\n"
                         + out + "<!-- status_table:end -->\n")
        self.assertEqual(os.stat(status).st_mode & 0o777, 0o644)
        self.assertEqual(sorted(os.listdir(self.root)).count("STATUS.md"), 1)
        self.assertFalse([n for n in os.listdir(self.root) if n.endswith(".tmp")])

    def test_update_without_markers_exits_2(self):
        status = write(self.root, "STATUS.md", "# S\n")
        self.assertEqual(run("--root", self.root, "--update", str(status))[0], 2)
        self.assertEqual(status.read_text(), "# S\n")

    def test_update_is_skipped_when_nfr10_fails(self):
        write(self.root, "src/main/d.c", "#ifdef DW1_PORT\n#endif\n")
        text = "<!-- status_table:begin -->\nold\n<!-- status_table:end -->\n"
        status = write(self.root, "STATUS.md", text)
        self.assertEqual(run("--root", self.root, "--update", str(status))[0], 1)
        self.assertEqual(status.read_text(), text)


if __name__ == "__main__":
    unittest.main()

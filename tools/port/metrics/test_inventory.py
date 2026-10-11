#!/usr/bin/env python3
"""Tests for inventory.py. Run: python3 -I -B tools/port/metrics/test_inventory.py"""
import pathlib
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import inventory  # noqa: E402
import isr_spins  # noqa: E402
import ps1_literals  # noqa: E402


class Inventory(unittest.TestCase):
    def test_strip_keeps_offsets(self):
        src = 'a = "x(y)"; // f(z)\n/* g(\n) */ b;'
        out = inventory.strip_c(src)
        self.assertEqual(len(out), len(src))
        self.assertEqual(out.count("\n"), src.count("\n"))
        self.assertNotIn("(", out)

    def test_merge_keeps_status(self):
        rows = inventory.number([("f.c", "k", "t", 3, inventory.PENDING),
                                 ("f.c", "k", "t", 9, inventory.PENDING)])
        tsv = pathlib.Path(tempfile.mkdtemp(), "x.tsv")
        rows[1][5] = "port_yield"
        tsv.write_text(inventory.render(rows))
        fresh = inventory.number([("f.c", "k", "t", 4, inventory.PENDING),
                                  ("f.c", "k", "t", 10, inventory.PENDING)])
        merged, lost = inventory.merge(fresh, inventory.load(tsv))
        self.assertEqual([r[5] for r in merged], [inventory.PENDING, "port_yield"])
        self.assertEqual([r[3] for r in merged], ["1", "2"])
        self.assertEqual(lost, [])
        _, lost = inventory.merge(inventory.number([("f.c", "k", "other", 4, inventory.PENDING)]),
                                  inventory.load(tsv))
        self.assertEqual(lost, [(("f.c", "k", "t", "2"), "port_yield")])
        # An old pending row takes the freshly computed default.
        fresh = inventory.number([("f.c", "k", "t", 3, "traducido: rama PS1")])
        old = {("f.c", "k", "t", "1"): inventory.PENDING}
        self.assertEqual(inventory.merge(fresh, old)[0][0][5], "traducido: rama PS1")

    def test_strip_escapes_and_unterminated(self):
        src = "a = '\\''; b = \"\\\\\"; f(x);\n#if 0\ndon't\n#endif\ng(y);\n"
        out = inventory.strip_c(src)
        self.assertEqual(len(out), len(src))
        self.assertEqual([l.find("(") >= 0 for l in out.split("\n")], [True, False, False, False, True, False])

    def test_enclosing_function(self):
        text = "int g;\nvoid foo(int a)\n{\n\tif (a) {\n\t\tx;\n\t}\n}\n"
        self.assertEqual(inventory.enclosing_function(text, text.index("x;")), "foo")
        self.assertEqual(inventory.enclosing_function(text, text.index("int g")), "")
        knr = "int f(a)\n\tchar a;\n{\n\twhile (a) { y; }\n}\n"
        self.assertEqual(inventory.enclosing_function(knr, knr.index("y;")), "f")

    def test_literal_defaults(self):
        root = pathlib.Path(tempfile.mkdtemp())
        (root / "src").mkdir()
        (root / "src/a.c").write_text(
            "#define B ((char *)0x80010000)\n"
            "int f(int x) { return x & 0x80000000; } /* 0x80020000 in a comment */\n"
            "#ifdef DW1_PORT\n#define C ((char *)PS1_RAM_ADDR(0x80030000))\n"
            "#else\n#define C ((char *)0x80030000)\n#endif\n")
        (root / "src/a_bss_us.c").write_text("void *p = (void *)0x80040000;\n")
        rows = [(f, lit, st) for f, lit, _, _, st in
                ps1_literals.findings(["src/a.c", "src/a_bss_us.c"], root)]
        self.assertEqual(rows, [
            ("src/a.c", "0x80010000", inventory.PENDING),
            ("src/a.c", "0x80000000", "no aplica: constante"),
            ("src/a.c", "0x80030000", "traducido"),
            ("src/a.c", "0x80030000", "traducido: rama PS1"),
            ("src/a_bss_us.c", "0x80040000", "no aplica: residuo del REL")])
        self.assertEqual(ps1_literals.findings([], root), [])

    def test_block_pairing(self):
        root = pathlib.Path(tempfile.mkdtemp())
        (root / "src").mkdir()
        (root / "src/b.c").write_text(
            "#ifdef DW1_PORT\n#define A ((char *)PS1_RAM_ADDR(0x80054838))\n"
            "#define B ((char *)PS1_RAM_ADDR(0x80054838))\n#else\n"
            "#define A ((char *)0x80054838)\n#define B ((char *)0x80054d00)\n#endif\n")
        (root / "src/c.c").write_text(
            "#ifdef DW1_PORT\n#define A ((char *)PS1_RAM_ADDR(0x80010000) + 0x80020000)\n"
            "#else\n#define A ((char *)0x80010000 + 0x80020000)\n#endif\n")
        _, errors = ps1_literals.scan(["src/b.c"], root)
        self.assertEqual(len(errors), 1)
        self.assertIn("src/b.c:2", errors[0])
        rows, errors = ps1_literals.scan(["src/c.c"], root)
        self.assertEqual(errors, [])
        self.assertIn(("0x80020000", inventory.PENDING), [(r[1], r[4]) for r in rows])

    def test_port_branches(self):
        lines = ["a", "#ifdef DW1_PORT", "p", "#if X", "q", "#endif", "#else", "r", "#endif", "s"]
        self.assertEqual(ps1_literals.port_branches(lines),
                         ["", "", "port", "", "port", "", "", "ps1", "", ""])
        self.assertEqual(ps1_literals.port_branches(
            ["# ifndef DW1_PORT", "a", "#else", "b", "#endif"]), ["", "ps1", "", "port", ""])
        self.assertEqual(ps1_literals.port_branches(
            ["#ifdef DW1_PORT", "a", "#elif X", "b", "#else", "c", "#endif"]),
            ["", "port", "", "", "", "", ""])
        rows = {(f, lit, st) for f, lit, _, _, st in ps1_literals.findings()
                if f == "include/dw/main.h" and lit == "0x80010000"}
        self.assertEqual(rows, {("include/dw/main.h", "0x80010000", "traducido"),
                                ("include/dw/main.h", "0x80010000", "traducido: rama PS1")})

    def test_literal_suffix(self):
        self.assertTrue(ps1_literals.LITERAL.search("x = 0x80010000U;"))
        self.assertFalse(ps1_literals.LITERAL.search("x = 0x80200000;"))

    def test_no_pending_fails(self):
        src = inventory.ROOT / "tools" / "port" / "metrics" / "isr_spins.tsv"
        tsv = pathlib.Path(tempfile.mkdtemp(), "pending.tsv")
        lines = src.read_text(encoding="utf-8").split("\n")
        c = lines[1].split("\t")
        c[5] = inventory.PENDING
        tsv.write_text("\n".join([lines[0], "\t".join(c)] + lines[2:]), encoding="utf-8")
        r = subprocess.run([sys.executable, "-I", "-B", str(src.with_suffix(".py")),
                            "--no-pending", "--tsv", str(tsv)], capture_output=True, text=True)
        self.assertNotEqual(r.returncode, 0)

    def test_versioned_inventories_are_current(self):
        for name, mod in (("isr_spins", isr_spins), ("ps1_literals", ps1_literals)):
            tsv = inventory.ROOT / "tools" / "port" / "metrics" / f"{name}.tsv"
            rows, lost = inventory.merge(inventory.number(mod.findings()), inventory.load(tsv))
            self.assertEqual(lost, [])
            self.assertEqual(tsv.read_text(encoding="utf-8"), inventory.render(rows))


    def test_check_fails_on_stale_inventory(self):
        src = inventory.ROOT / "tools" / "port" / "metrics" / "ps1_literals.tsv"
        stale = pathlib.Path(tempfile.mkdtemp(), "stale.tsv")
        stale.write_text("".join(src.read_text().splitlines(keepends=True)[:-1]))
        r = subprocess.run([sys.executable, "-I", "-B", str(src.with_suffix(".py")), "--check",
                            "--tsv", str(stale)], capture_output=True, text=True)
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("is stale", r.stderr)


class Spins(unittest.TestCase):
    def found(self, code):
        return [(k, c) for _, k, _, c, body in isr_spins.loops(inventory.strip_c(code))
                if not isr_spins.has_call(body)]

    def test_flag_wait_found(self):
        self.assertEqual(self.found("void f(E *e){ while (e->flag == 0) { } }"),
                         [("while", "(e->flag == 0)")])

    def test_loop_with_call_ignored(self):
        self.assertEqual(self.found("void f(){ while (x) { VSync(0); } }"), [])

    def test_indexed_call_ignored(self):
        self.assertEqual(self.found("void f(){ while (p) { jtbl[op](); } }"), [])

    def test_do_while_tail_not_duplicated(self):
        self.assertEqual(self.found("void f(){ do { i++; } while (i < 3); }"),
                         [("do", "(i < 3)")])

    def test_counted_for_ignored(self):
        self.assertEqual(self.found("void f(){ for (i = 0; i < 3; i++) { a[i] = 0; } }"), [])
        self.assertEqual(self.found("void f(){ for (;;) { if (g) break; } }"),
                         [("for", "(;;)")])


if __name__ == "__main__":
    unittest.main()

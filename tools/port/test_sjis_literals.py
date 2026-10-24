#!/usr/bin/env python3
"""L1 checks for the port's CP932 source conversion."""
import ast
import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('sjis_literals', HERE / 'sjis_literals.py')
sjis = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sjis)


class Literals(unittest.TestCase):
    def body(self, text):
        return sjis.convert(text, 'src/example.c').split('\n', 1)[1]

    def test_literals_and_character(self):
        self.assertEqual(self.body('"０１２３４"; \'　\';'),
                         '"\\202\\117\\202\\120\\202\\121\\202\\122\\202\\123"; \'\\201\\100\';')

    def test_outside_literals(self):
        text = '/* ０ " */\n// "１"\\\n２\nint 変数;\n#include "ascii.h"\n'
        self.assertEqual(self.body(text), text)

    def test_directives(self):
        text = '#include \"日本.h\"\n#define TEXT \"０\"\\\n \"１\"\n\"２\"\n'
        expected = text
        for c in '０１２':
            expected = expected.replace(c, ''.join(f'\\{b:03o}' for b in c.encode('cp932')))
        self.assertEqual(self.body(text), expected)
        self.assertEqual(self.body("#error can't\n\"０\"\n"), "#error can't\n\"\\202\\117\"\n")

    def test_escapes(self):
        text = r'''"０\"１\\２\n\123" '\'' '\\' "３"'''
        expected = text
        for c in '０１２３':
            expected = expected.replace(c, ''.join(f'\\{b:03o}' for b in c.encode('cp932')))
        self.assertEqual(self.body(text), expected)

    def test_gen_text_equivalence(self):
        # Extract just the function: do not import gen_text or depend on PyYAML/private APIs.
        tree = ast.parse((HERE.parent / 'gen_text.py').read_text())
        function = next(n for n in tree.body if isinstance(n, ast.FunctionDef) and n.name == '__c_string')
        namespace = {}
        exec(compile(ast.Module(body=[function], type_ignores=[]), 'gen_text.py', 'exec'), namespace)
        text = '０１２３４　アイウｱ'
        self.assertEqual(self.body('"' + text + '"'), namespace['__c_string'](text, 'test'))

    def test_cli_and_error(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / 'input.c'
            target = Path(directory) / 'nested/output.c'
            source.write_text('/* 😀 */\n"０"\n')
            run = subprocess.run([sys.executable, str(HERE / 'sjis_literals.py'), str(source), str(target)], capture_output=True)
            self.assertEqual(run.returncode, 0, run.stderr)
            self.assertTrue(target.read_text().startswith(f'#line 1 "{source}"\n'))
            source.write_text('/* ok */\n"😀"\n')
            run = subprocess.run([sys.executable, str(HERE / 'sjis_literals.py'), str(source), str(target)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 1)
            self.assertIn(f'{source}:2:', run.stderr)


if __name__ == '__main__':
    unittest.main()

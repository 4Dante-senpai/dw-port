import pathlib
import sys
import tempfile
import unittest
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parent))
import press_tables
class Tables(unittest.TestCase):
    def test_regions(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=pathlib.Path(tmp)
            (root/'main.yaml').write_text('segments: [{name: main, start: 0x80, vram: 0x1000}]')
            (root/'symbols.txt').write_text('data_2 = 0x1400;\nmdec_coef = 0x1200;\nmdec_iq = 0x1100;\nalias = 0x1100;\nend = 0x1800;\n')
            self.assertEqual(press_tables.values(root),dict(mdec_iq=(0x180,0x100),mdec_coef=(0x280,0x200),data_2=(0x480,0x400)))
            output=root/'generated/tables.h'; press_tables.generate(root,output)
            self.assertIn('DW1_PRESS_MDEC_COEF_SIZE 0x200u',output.read_text())
            stamp=output.stat().st_mtime_ns; press_tables.generate(root,output)
            self.assertEqual(stamp,output.stat().st_mtime_ns)
            (root/'symbols.txt').write_text('mdec_iq = 0x1100;\n')
            with self.assertRaises((KeyError,StopIteration)): press_tables.values(root)
if __name__=='__main__': unittest.main()

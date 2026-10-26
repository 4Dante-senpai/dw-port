#!/usr/bin/env python3
"""Synthetic MIPS checks for the SHOP static audit; no disc data."""
import unittest
import importlib.util
from pathlib import Path

spec = importlib.util.spec_from_file_location('shop_overlay', Path(__file__).with_name('shop_overlay.py'))
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
LO, normalize, scan = module.LO, module.normalize, module.scan


class ShopAuditTests(unittest.TestCase):
    def test_direct_and_signed_address_candidates(self):
        direct, pointers = scan({0x80090000: 0x0c000000 | ((LO >> 2) & 0x3ffffff),
            0x80090004: 0x3c028009, 0x80090008: 0x24428800})
        self.assertEqual(direct[0]['target'], hex(LO))
        self.assertEqual(pointers, [])
        # Signed low halves must borrow from the high half.
        _, pointers = scan({0x80090000: 0x3c028009, 0x80090004: 0x24428000})
        self.assertEqual(pointers[0]['address'], '0x80088000')

    def test_clobbered_register_is_not_a_candidate(self):
        self.assertEqual(scan({0x80090000: 0x3c028008, 0x80090004: 0x24020000,
                               0x80090008: 0x24420840})[1], [])

    def test_delay_slot_sees_registers_before_branch(self):
        _, pointers = scan({0x80090000: 0x3c048008, 0x80090004: 0x0c040000,
                            0x80090008: 0x24840840, 0x8009000c: 0x24840004})
        self.assertEqual([(row['pc'], row['address']) for row in pointers],
                         [('0x80090008', '0x80080840')])

    def test_cop2_register_reads_invalidate_destination(self):
        for rs in (0, 2):
            with self.subTest(rs=rs):
                self.assertEqual(scan({0x80090000: 0x3c028008,
                    0x80090004: (0x12 << 26) | (rs << 21) | (2 << 16),
                    0x80090008: 0x24420840})[1], [])

    def test_normalization_stops_at_special_register_write(self):
        code = [0x3c028008, 0x00641021, 0x24420840]  # lui; addu v0,v1,a0; addiu
        self.assertEqual(normalize(code), [0x3c020000, code[1], code[2]])

    def test_normalization_masks_relocations_and_keeps_constants(self):
        first = [0x0c042345, 0x3c028008, 0x24420840, 0x24040010]
        second = [0x0c012345, 0x3c028011, 0x24421234, 0x24040010]
        self.assertEqual(normalize(first), normalize(second))
        second[-1] += 1
        self.assertNotEqual(normalize(first), normalize(second))


if __name__ == '__main__':
    unittest.main()

# SPDX-License-Identifier: MIT
import importlib.util
from pathlib import Path
import struct
import unittest
ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("carrier", ROOT / "recipes-bsp/u-boot/files/chain/ce-carrier.py")
c = importlib.util.module_from_spec(spec)
spec.loader.exec_module(c)

def code(size=80, address=0x40021000):
    data = bytearray(size)
    struct.pack_into("<I", data, 0, 0xea00000e)
    struct.pack_into("<I", data, 0x40, address)
    return bytes(data)

def envelope(payload):
    return c.build(payload, c.CE_BASE, c.CE_BASE)

class Carrier(unittest.TestCase):
    def modern_elf(self, data, address=0x40021000):
        elf = bytearray(124)
        elf[:7] = b'\x7fELF\x01\x01\x01'
        struct.pack_into('<HH', elf, 16, 2, 40)
        struct.pack_into('<II', elf, 24, address, 52)
        struct.pack_into('<I', elf, 32, 84)
        struct.pack_into('<HH', elf, 42, 32, 1)
        struct.pack_into('<HH', elf, 46, 40, 1)
        struct.pack_into('<8I', elf, 52, 1, 124, address, address,
                         len(data), len(data), 5, 4)
        struct.pack_into('<10I', elf, 84, 0, 1, 6, address, 124,
                         len(data), 0, 0, 4, 0)
        return bytes(elf) + data

    def test_modern_elf_carrier(self):
        data = code(0x30000, address=0)
        payload = c.make_payload(data, modern_elf=self.modern_elf(data))
        c.validate_cold_parser(envelope(payload))

    def test_modern_objcopy_gap_fill(self):
        data = bytearray(code())
        data[40:44] = b'\xff' * 4
        elf = bytearray(self.modern_elf(data)[:84]) + bytearray(80)
        struct.pack_into('<H', elf, 48, 2)
        struct.pack_into('<I', elf, 56, 164)
        struct.pack_into('<10I', elf, 84, 0, 1, 6, 0x40021000, 164,
                         40, 0, 0, 4, 0)
        struct.pack_into('<10I', elf, 124, 0, 1, 2, 0x4002102c, 208,
                         36, 0, 0, 4, 0)
        elf += data
        elf[204:208] = bytes(4)
        c.make_payload(data, modern_elf=elf)
        data[40] = 0
        with self.assertRaises(ValueError):
            c.make_payload(data, modern_elf=elf)

    def test_modern_elf_wrong_entry_or_payload(self):
        data = code()
        for elf in (b'', self.modern_elf(data, 0x42000000),
                    self.modern_elf(data[:-1] + b'X')):
            with self.assertRaises(ValueError):
                c.make_payload(data, modern_elf=elf)

    def test_modern_elf_truncation_and_invalid_segment(self):
        data = code()
        elf = self.modern_elf(data)
        for length in (0, 51, 83, len(elf) - 1):
            with self.assertRaises(ValueError):
                c.make_payload(data, modern_elf=elf[:length])
        for offset, value in ((60, 0), (64, 0), (76, 4)):
            bad = bytearray(elf)
            struct.pack_into('<I', bad, offset, value)
            with self.assertRaises(ValueError):
                c.make_payload(data, modern_elf=bad)

    def test_normal_boot_pointer_pair(self):
        payload = c.make_payload(code())
        self.assertEqual(struct.unpack_from("<II", payload, 0x44), (0x80020100, 0x100))
        c.validate_cold_parser(envelope(payload))

    def test_wrong_role(self):
        with self.assertRaises(ValueError): c.make_payload(code(address=0x46000000), chain=True)

    def test_short_payloads(self):
        for length in (0, 1, 63, 67):
            with self.assertRaises(ValueError): c.make_payload(bytes(length))

    def test_branch_vector_required(self):
        data = bytearray(code()); data[3] = 0
        with self.assertRaises(ValueError): c.make_payload(data)

    def test_legacy_size_limit(self):
        with self.assertRaises(ValueError): c.make_payload(code(0x20000))

    def test_chain_capacity_explicit(self):
        payload = c.make_payload(code(0x70000), chain=True)
        c.validate_cold_parser(envelope(payload))
        with self.assertRaises(ValueError): c.make_payload(code(0x100000), chain=True)

    def test_missing_relative_header(self):
        payload = bytearray(c.make_payload(code()))
        struct.pack_into("<I", payload, 0x48, 0)
        with self.assertRaises(ValueError): c.validate_cold_parser(envelope(payload))

    def test_bad_checksum_and_suffix(self):
        blob = bytearray(envelope(c.make_payload(code())))
        blob[-20] ^= 1
        with self.assertRaises(ValueError): c.validate_cold_parser(blob)
        with self.assertRaises(ValueError): c.validate_cold_parser(envelope(c.make_payload(code())) + b"suffix")

    def test_invalid_module_pointer(self):
        payload = bytearray(c.make_payload(code()))
        struct.pack_into("<I", payload, 0x164, 0xffffffff)
        with self.assertRaises(ValueError): c.validate_cold_parser(envelope(payload))

    def test_integrity_does_not_move_header(self):
        payload = c.make_payload(code(), integrity=True)
        c.validate_cold_parser(envelope(payload))
        self.assertEqual(struct.unpack_from("<I", payload, 0x300)[0], c.INTEGRITY_MAGIC)

if __name__ == "__main__": unittest.main()

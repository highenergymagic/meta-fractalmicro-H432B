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

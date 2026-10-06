# SPDX-License-Identifier: MIT
import importlib.util
from pathlib import Path
import unittest
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location("memory",ROOT/"recipes-bsp/u-boot/files/nand/check-memory.py")
memory=importlib.util.module_from_spec(spec)
spec.loader.exec_module(memory)
MAP="0x46000000 __image_copy_start = .\n0x46045340 __bss_end__ = .\n"
class MemoryContract(unittest.TestCase):
    def test_current_map(self): memory.check(MAP)
    def test_old_heap_collision(self):
        with self.assertRaises(ValueError): memory.check(MAP,heap_start=0x46800000)
    def test_large_binary_rejected(self):
        with self.assertRaises(ValueError): memory.check(MAP.replace("0x46045340","0x46100004"))
    def test_wrong_role_rejected(self):
        with self.assertRaises(ValueError): memory.check(MAP.replace("0x46000000","0x40021000"))
    def test_chain_map(self):
        memory.check("0x40021000 __image_copy_start = .\n0x4007fc80 __bss_end__ = .\n",chain=True)
    def test_chain_wrong_role(self):
        with self.assertRaises(ValueError): memory.check(MAP,chain=True)
    def test_chain_oversize(self):
        with self.assertRaises(ValueError):
            memory.check("0x40021000 __image_copy_start = .\n0x400a1004 __bss_end__ = .\n",chain=True)
if __name__=="__main__": unittest.main()

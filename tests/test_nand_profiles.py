# SPDX-License-Identifier: MIT
import importlib.util
from pathlib import Path
import unittest
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location("profile",ROOT/"recipes-kernel/linux/files/nand-profile.py")
profile=importlib.util.module_from_spec(spec)
spec.loader.exec_module(profile)
class NandProfiles(unittest.TestCase):
    def setUp(self): self.text=(ROOT/"recipes-kernel/linux/files/s5pv210-hims-u2.dts").read_text()
    def test_readonly_default(self): self.assertEqual(profile.apply(self.text,"readonly"),self.text)
    def test_scratch_is_exactly_last_pool_block(self):
        self.assertIn("hims,write-window = <0x1fee0000 0x00020000>;",
                      profile.apply(self.text,"scratch"))
    def test_ubi_excludes_boot_and_tail(self):
        self.assertIn("hims,write-window = <0x00400000 0x1fb00000>;",
                      profile.apply(self.text,"ubi"))
    def test_invalid_or_double_application_refused(self):
        with self.assertRaises(ValueError): profile.apply(self.text,"all")
        with self.assertRaises(ValueError): profile.apply(profile.apply(self.text,"scratch"),"ubi")
if __name__=="__main__": unittest.main()

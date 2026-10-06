# SPDX-License-Identifier: MIT
from pathlib import Path
import unittest
ROOT = Path(__file__).resolve().parents[1]

class NandProfile(unittest.TestCase):
    def test_profile_is_separate_ram_role(self):
        text = (ROOT / "recipes-bsp/u-boot/u-boot-h432b-nand-profile_2012.10.bb").read_text()
        self.assertIn('H432B_UBOOT_ROLE = "ram57-nand-profile"', text)
        self.assertIn("CONFIG_U2_NAND_PROFILE", text)
        self.assertNotIn("nand-auto", text)

    def test_no_fake_milliseconds_or_data_cache_enable(self):
        text = (ROOT / "recipes-bsp/u-boot/files/nand/u2nand.c").read_text()
        self.assertNotIn("get_timer(", text)
        self.assertNotIn("dcache_enable(", text)
        self.assertIn("host elapsed time required", text)
        self.assertIn("if(mode==3 && !profile_attached)", text)
        self.assertIn("mtd->write=ro_write", text)

if __name__ == "__main__":
    unittest.main()

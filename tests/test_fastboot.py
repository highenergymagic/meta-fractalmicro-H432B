# SPDX-License-Identifier: MIT
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / "recipes-bsp/u-boot"


class FastbootIntegration(unittest.TestCase):
    def test_separate_ram_role(self):
        text = (BASE / "u-boot-h432b-fastboot_2012.10.bb").read_text()
        self.assertIn('H432B_UBOOT_ROLE = "ram53-fastboot-only"', text)
        self.assertIn('H432B_UBOOT_ENTRY = "46000000"', text)
        self.assertIn("0001-ram-loader-rev52.patch", text)
        self.assertIn("test-fastboot", text)
        for name in ("u-boot-h432b_2012.10.bb",):
            self.assertNotIn("fastboot", (BASE / name).read_text())

    def test_descriptor_and_teardown_hooks(self):
        text = (BASE / "files/0003-fastboot-ram53.patch").read_text()
        self.assertIn("9,USB_DT_INTERFACE,1,0,2,0xff,0x42,0x03,5", text)
        self.assertIn("u2fastboot_disable()", text)
        self.assertIn("u2fastboot_poll()", text)

    def test_storage_failure_is_explicit(self):
        text = (BASE / "files/fastboot/u2fastboot.c").read_text()
        self.assertIn('response("FAIL","storage backend not qualified; RAM only")', text)
        self.assertNotIn("nand_write", text)
        self.assertNotIn("nand_erase", text)
        self.assertIn("crc32(0,p,valid)!=image_crc", text)
        self.assertIn("if (tx_done)", text)


if __name__ == "__main__":
    unittest.main()

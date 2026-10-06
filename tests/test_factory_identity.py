# SPDX-License-Identifier: MIT
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / "recipes-bsp/u-boot"

class FactoryIdentity(unittest.TestCase):
    def test_shared_linux_stage_integration(self):
        recipe = (BASE / "u-boot-h432b-fastboot_2012.10.bb").read_text()
        self.assertIn('SRC_URI:append = " file://0010-factory-identity.patch', recipe)
        self.assertEqual(recipe.count("do_configure:prepend()"), 1)
        self.assertEqual(recipe.count("do_compile:prepend()"), 1)
        self.assertIn("${B}/test-identity", recipe)
        patch = (BASE / "files/0010-factory-identity.patch").read_text()
        self.assertIn("CONFIG_OF_BOARD_SETUP", patch)
        self.assertIn("+\tu2_identity_capture();", patch)

    def test_no_factory_writes_or_fake_serial(self):
        source = (BASE / "files/identity/u2identity.c").read_text()
        for forbidden in ("nand_write", "nand_erase", "writel(", "writeb(",
                          '"serial-number"'):
            self.assertNotIn(forbidden, source)
        for field in ('"mac-address"', '"local-mac-address"',
                      '"fractalmicro,board-id"', '"factory-ethernet-mac"'):
            self.assertIn(field, source)
        self.assertIn("fdt_node_check_compatible", source)
        self.assertIn("0x40000800UL", source)

if __name__ == "__main__":
    unittest.main()

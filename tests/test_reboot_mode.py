# SPDX-License-Identifier: MIT
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
UB = ROOT / "recipes-bsp/u-boot"

class RebootModeContract(unittest.TestCase):
    def test_experimental_role(self):
        recipe = (UB / "u-boot-h432b-reboot-test_2012.10.bb").read_text()
        self.assertIn('H432B_UBOOT_ROLE = "ram-reboot-test"', recipe)
        self.assertIn("RAM ONLY", recipe)
        self.assertIn("test-bootmode", recipe)

    def test_normal_recipe_does_not_enable_test(self):
        for name in ("u-boot-h432b-nand-auto_2012.10.bb",
                     "u-boot-h432b-chain_2012.10.bb"):
            self.assertNotIn("reboot-mode-test", (UB / name).read_text())

    def test_consumed_before_nand(self):
        patch = (UB / "files/0008-reboot-mode-test.patch").read_text()
        self.assertIn('u2bootmode && u2nandinit ident', patch)
        self.assertIn('u2nandboot a; u2 probe', patch)

    def test_probe_readonly(self):
        probe = (ROOT / "recipes-support/h432b-reboot-probe/files/reboot-probe.c").read_text()
        self.assertIn("O_RDONLY | O_SYNC", probe)
        self.assertNotIn("PROT_WRITE", probe)
        self.assertIn("volatile const uint32_t", probe)

    def test_linux_uses_matching_protocol(self):
        dts = (ROOT / "recipes-kernel/linux/files/s5pv210-hims-u2-reboot-test.dts").read_text()
        self.assertIn("offset = <0x701c>", dts)
        self.assertIn("mode-normal = <0x48344e4d>", dts)
        self.assertIn("mode-bootloader = <0x48344642>", dts)
        self.assertIn("mode-fastboot = <0x48344642>", dts)
        self.assertIn('"syscon", "simple-mfd"', dts)
        header = (UB / "files/reboot/bootmode.h").read_text()
        self.assertIn("H432B_MODE_NORMAL 0x48344e4dU", header)
        self.assertIn("H432B_MODE_FASTBOOT 0x48344642U", header)

    def test_kernel_is_isolated(self):
        recipe = (ROOT / "recipes-kernel/linux/linux-h432b-reboot-test_6.12.111.bb").read_text()
        self.assertIn('PROVIDES:remove = "virtual/kernel"', recipe)
        self.assertIn('KERNEL_PACKAGE_NAME = "kernel-reboot-test"', recipe)
        normal = (ROOT / "recipes-kernel/linux/linux-h432b_6.12.111.bb").read_text()
        self.assertNotIn("reboot-test", normal)

    def test_retention_guard(self):
        probe = (ROOT / "recipes-support/h432b-reboot-probe/files/reboot-retention.c").read_text()
        self.assertIn("before != expected", probe)
        self.assertIn("--arm-retention-test", probe)
        self.assertIn("--clear-retention-test", probe)
        self.assertNotIn("/dev/mtd", probe)

if __name__ == "__main__":
    unittest.main()

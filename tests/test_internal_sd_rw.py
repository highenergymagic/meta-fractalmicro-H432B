# SPDX-License-Identifier: MIT
from pathlib import Path
import unittest
ROOT = Path(__file__).resolve().parents[1]
FILES = ROOT / "recipes-kernel/linux/files"

class InternalSdWriteTests(unittest.TestCase):
    def test_opt_in_controller_scope(self):
        text = (FILES / "s5pv210-hims-u2-sd-rw-test.dts").read_text()
        self.assertIn("hims,internal-sd-write-test", text)
        self.assertIn("&sdhci1 {", text)
        self.assertEqual(text.count("/delete-property/"), 1)
        self.assertIn("/delete-property/ hims,read-only-probe", text)
        self.assertNotIn("&sdhci3", text)
        self.assertNotIn("nand", text.lower())
        base = (FILES / "s5pv210-hims-u2.dts").read_text()
        self.assertIn("hims,read-only-probe", base.split("&sdhci1 {")[1].split("};")[0])

    def test_nand_guard(self):
        recipe = (ROOT / "recipes-kernel/linux/linux-h432b-sd-rw-test_6.12.111.bb").read_text()
        self.assertIn('d.getVar("H432B_NAND_PROFILE") != "readonly"', recipe)
        self.assertIn('KERNEL_PACKAGE_NAME = "kernel-sd-rw-test"', recipe)

    def test_bounded_filesystem_test(self):
        text = (ROOT / "tests/check-internal-sd-rw.sh").read_text()
        for guard in ("--write-test", "hims,internal-sd-write-test", "eb100000.mmc",
                      "/proc/self/mountinfo", "holders/*", "conv=excl,fsync",
                      "iflag=direct", "ro_mode", "umount", "sha256sum"):
            self.assertIn(guard, text)
        for forbidden in ("mkfs", "of=/dev/", "rm -r", "wipefs", "discard"):
            self.assertNotIn(forbidden, text)
        self.assertIn('for n in 0 1 2 3 4 5 6 7', text)
        self.assertIn('bs=1M count=8', text)

if __name__ == "__main__":
    unittest.main()

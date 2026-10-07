# SPDX-License-Identifier: MIT
"""Layout and instruction-contract guards, not a suspend qualification."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
FILES = ROOT / "recipes-kernel/linux/files"

class ResumeBridgeTests(unittest.TestCase):
    def test_memory_excludes_fixed_entry(self):
        text = (FILES / "s5pv210-hims-u2-resume-test.dts").read_text()
        base, size = (int(x, 16) for x in re.search(
            r"linux,usable-memory-range = <(0x[0-9a-f]+) (0x[0-9a-f]+)>", text).groups())
        self.assertEqual(base, 0x40200000)
        self.assertEqual(base + size, 0x50000000)
        self.assertLess(0x40020000 + 4096, base)
        self.assertIn("reg = <0x40020000 0x1000>", text)
        self.assertIn("no-map;", text)

    def test_arm_stub_contract(self):
        patch = (FILES / "0009-factory-resume-test.patch").read_text()
        code = [0xe59f0000, 0xe590f000, 0xe010f000]
        for word in code:
            self.assertIn(hex(word), patch)
        # First ARM LDR uses PC+8 with zero offset, to load word 2.
        self.assertEqual((code[0] >> 16) & 15, 15)
        self.assertEqual((code[0] >> 12) & 15, 0)
        self.assertEqual(code[0] & 4095, 0)
        # Second LDR loads PC from the INFORM0 address in r0.
        self.assertEqual((code[1] >> 16) & 15, 0)
        self.assertEqual((code[1] >> 12) & 15, 15)
        self.assertEqual(code[1] & 4095, 0)

    def test_layout_and_readback_gates(self):
        text = (FILES / "0009-factory-resume-test.patch").read_text()
        self.assertIn('of_property_read_bool(of_root, "hims,factory-resume-test")', text)
        self.assertIn("memblock_start_of_DRAM() != 0x40200000", text)
        self.assertIn("memblock_is_region_memory(0x40020000, PAGE_SIZE)", text)
        self.assertIn("resume bridge readback failed", text)

    def test_no_automatic_suspend(self):
        text = (FILES / "u2-resume-test.config").read_text()
        self.assertIn("# CONFIG_PM_TEST_SUSPEND is not set", text)
        self.assertIn("CONFIG_PM_SLEEP_DEBUG=y", text)

    def test_default_unchanged(self):
        text = (FILES / "s5pv210-hims-u2.dts").read_text()
        self.assertNotIn("hims,factory-resume-test;", text)
        self.assertIn("reg = <0x40000000 0x10000000>", text)
        recipe = (ROOT / "recipes-kernel/linux/linux-h432b_6.12.111.bb").read_text()
        self.assertNotIn("0009-factory-resume-test", recipe)

    def test_guarded_device_diagnostic(self):
        text = (ROOT / "tests/check-pm-devices.sh").read_text()
        self.assertIn("SDIO_ID=024C:8712", text)
        self.assertIn("eb300000.mmc", text)
        self.assertIn("eb100000.mmc/driver", text)
        self.assertIn('test "$found" = 1', text)
        self.assertIn('grep -q "\\\\[$level\\\\]" /sys/power/pm_test', text)
        self.assertIn("trap cleanup EXIT", text)
        self.assertNotIn("level=none", text)
        self.assertNotIn("/sys/power/disk", text)

    def test_wake_input(self):
        text = (FILES / "s5pv210-hims-u2-resume-test.dts").read_text()
        self.assertIn("&{/power-keys/power-button}", text)
        self.assertIn("wakeup-source;", text)
        self.assertNotIn("gpio-poweroff", text)

if __name__ == "__main__":
    unittest.main()

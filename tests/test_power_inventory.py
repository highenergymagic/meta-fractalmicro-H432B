# SPDX-License-Identifier: MIT
"""Guard the scope of the opt-in PMIC inventory experiment."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
FILES = ROOT / "recipes-kernel/linux/files"

class PowerInventoryTests(unittest.TestCase):
    def test_separate_kernel(self):
        text = (ROOT / "recipes-kernel/linux/linux-h432b-power-test_6.12.111.bb").read_text()
        self.assertIn('KERNEL_PACKAGE_NAME = "kernel-power-test"', text)
        self.assertIn('require linux-h432b-reboot-test_', text)

    def test_only_established_bus_pins(self):
        text = (FILES / "s5pv210-hims-u2-power-test.dts").read_text()
        self.assertIn('<&gpd1 4 (GPIO_ACTIVE_HIGH | GPIO_OPEN_DRAIN)>', text)
        self.assertIn('<&gpd1 5 (GPIO_ACTIVE_HIGH | GPIO_OPEN_DRAIN)>', text)
        self.assertNotIn('gpio-poweroff', text)
        self.assertNotIn('reg = <0x66>', text)
        self.assertNotIn('wakeup-source', text)

    def test_no_suspend_or_fault_injection(self):
        text = (FILES / "u2-power-test.config").read_text()
        self.assertIn('# CONFIG_SUSPEND is not set', text)
        self.assertIn('# CONFIG_I2C_GPIO_FAULT_INJECTOR is not set', text)

    def test_bounded_register_reads(self):
        text = (ROOT / "recipes-support/h432b-power-inventory/files/power-inventory.c").read_text()
        self.assertIn('reg < 2', text)
        self.assertIn('strcmp(buf, "i2c-pmic-inventory\\n")', text)
        self.assertIn('.addr = 0x66, .flags = 0, .len = 1, .buf = &reg', text)
        self.assertIn('.addr = 0x66, .flags = I2C_M_RD, .len = 1, .buf = &value', text)
        self.assertNotIn('I2C_SLAVE_FORCE', text)
        self.assertNotIn('/dev/mem', text)

    def test_not_default_device_tree(self):
        text = (FILES / "s5pv210-hims-u2.dts").read_text()
        self.assertNotIn('i2c-pmic-inventory', text)

if __name__ == "__main__":
    unittest.main()

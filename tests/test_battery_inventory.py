# SPDX-License-Identifier: MIT
"""Static contract tests, not hardware timing or electrical qualification."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
FILES = ROOT / "recipes-kernel/linux/files"
SOURCE = (FILES / "h432b-battery-inventory.c").read_text()
DTS = (FILES / "s5pv210-hims-u2-battery-test.dts").read_text()

class BatteryInventoryTests(unittest.TestCase):
    def test_board_pins(self):
        for name, pin, polarity in [
            ("charging", 0, "LOW"), ("secondary-present", 1, "LOW"),
            ("ac-present", 2, "LOW"), ("rx", 3, "HIGH"),
            ("pull-low", 4, "HIGH"),
        ]:
            self.assertIn(f"{name}-gpios = <&gpc0 {pin} GPIO_ACTIVE_{polarity}>", DTS)
        self.assertNotIn("usb-present", DTS)
        self.assertNotIn("wakeup-source", DTS)

    def test_scope(self):
        self.assertIn("DEVICE_ATTR(snapshot, 0400, snapshot_show, NULL)", SOURCE)
        self.assertNotIn("DEVICE_ATTR_RW", SOURCE)
        self.assertNotIn("ioremap", SOURCE)
        self.assertIn("devm_power_supply_register", SOURCE)
        self.assertNotIn(".set_property", SOURCE)
        self.assertNotIn("POWER_SUPPLY_PROP_PRESENT", SOURCE)
        self.assertNotIn("POWER_SUPPLY_PROP_MODEL_NAME", SOURCE)
        probe = SOURCE.split("static int battery_inventory_probe", 1)[1]
        self.assertNotIn("battery_read_rom(", probe)
        self.assertNotIn("battery_write_byte(", probe)
        self.assertNotIn("i2c", DTS)

    def test_command_allowlist(self):
        literals = re.findall(r"battery_write_byte\(b, (0x[0-9a-f]+)\)", SOURCE)
        self.assertEqual(literals, ["0x33", "0x55", "0x69", "0x06", "0x55", "0x69"])
        self.assertNotIn("0xcc", SOURCE.lower())
        self.assertNotIn("0x6c", SOURCE.lower())

    def test_register_snapshot_bounds(self):
        self.assertIn("DEVICE_ATTR(registers, 0400, registers_show, NULL)", SOURCE)
        self.assertIn("address == 0x01 && len == 27", SOURCE)
        self.assertIn("address == 0x60 && len == 29", SOURCE)
        self.assertIn("rom[0] != 0x32", SOURCE)
        self.assertIn("pass < 2", SOURCE)
        self.assertIn("parameters_equal=%u", SOURCE)
        poll = SOURCE.split("static void battery_poll(", 1)[1].split("static void battery_stop_poll", 1)[0]
        self.assertNotIn("battery_read_window", poll)
        self.assertNotIn("registers_show", poll)

    def test_validation(self):
        self.assertIn("battery_crc(rom, 8)", SOURCE)
        self.assertIn("memcmp(rom, again, sizeof(rom))", SOURCE)
        self.assertIn("*capacity <= 100", SOURCE)
        self.assertIn("capacity != confirm", SOURCE)
        self.assertIn("rom[0] == 0x32 || rom[0] == 0x3d", SOURCE)
        self.assertIn("0x8c", SOURCE)

    def test_timing_and_release(self):
        self.assertIn("raw_spin_lock_irqsave", SOURCE)
        self.assertIn("gpiod_cansleep(b->rx)", SOURCE)
        self.assertIn("gpiod_cansleep(b->pull_low)", SOURCE)
        self.assertIn('devm_gpiod_get(dev, "pull-low", GPIOD_OUT_LOW)', SOURCE)
        self.assertIn("gpiod_set_value(b->pull_low, 0);", SOURCE)

    def test_default_unchanged(self):
        default = (FILES / "s5pv210-hims-u2.dts").read_text()
        self.assertNotIn("battery-inventory", default)
        recipe = (ROOT / "recipes-kernel/linux/linux-h432b-battery-test_6.12.111.bb").read_text()
        self.assertIn('KERNEL_PACKAGE_NAME = "kernel-battery-test"', recipe)
        self.assertIn("# CONFIG_SUSPEND is not set", recipe)

if __name__ == "__main__":
    unittest.main()

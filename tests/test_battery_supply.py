# SPDX-License-Identifier: MIT
"""Registration/cache contract checks; the recipe also executes C policy tests."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
FILES = ROOT / "recipes-kernel/linux/files"
SOURCE = (FILES / "h432b-battery-inventory.c").read_text()
POLICY = (FILES / "h432b-battery-policy.h").read_text()

class BatterySupplyTests(unittest.TestCase):
    def test_read_only_standard_properties(self):
        props = SOURCE.split("static enum power_supply_property battery_properties[]", 1)[1].split("};", 1)[0]
        self.assertIn("POWER_SUPPLY_PROP_CAPACITY", props)
        self.assertIn("POWER_SUPPLY_PROP_STATUS", props)
        self.assertEqual(props.count("POWER_SUPPLY_PROP_"), 6)
        self.assertIn('.name = "h432b-battery"', SOURCE)
        self.assertIn("POWER_SUPPLY_TYPE_BATTERY", SOURCE)
        self.assertNotIn(".set_property", SOURCE)

    def test_measurements(self):
        for name in ("VOLTAGE_NOW", "TEMP", "CURRENT_NOW", "CURRENT_AVG"):
            self.assertIn("POWER_SUPPLY_PROP_" + name, SOURCE)
        self.assertIn("!fresh || sample.measurement_error", SOURCE)
        self.assertIn("sense != confirm", SOURCE)
        self.assertIn("rom[0] != 0x32", SOURCE)
        self.assertIn("conductance <= 0", POLICY)
        self.assertIn("current_raw * 25 * conductance / 16", POLICY)

    def test_no_fabricated_values(self):
        self.assertNotIn("POWER_SUPPLY_STATUS_FULL", SOURCE)
        self.assertIn("return -ENODATA", SOURCE)
        self.assertIn("POWER_SUPPLY_STATUS_UNKNOWN", SOURCE)
        self.assertIn("capacity >= 0 && capacity <= 100", POLICY)
        self.assertIn("charging < 0 || primary < 0 || secondary < 0", POLICY)

    def test_cache_and_events(self):
        self.assertIn("b->sampled + 15 * HZ", SOURCE)
        self.assertIn("5 * HZ", SOURCE)
        self.assertIn("power_supply_changed(b->psy)", SOURCE)
        getter = SOURCE.split("static int battery_get_property", 1)[1].split("static const", 1)[0]
        self.assertNotIn("battery_sample_read", getter)
        self.assertIn("mutex_lock", getter)

    def test_cleanup(self):
        self.assertIn("cancel_delayed_work_sync(&b->poll)", SOURCE)
        probe = SOURCE.split("static int battery_inventory_probe", 1)[1]
        self.assertLess(probe.index("devm_power_supply_register"),
                        probe.index("devm_add_action_or_reset(dev, battery_stop_poll"))
        self.assertLess(probe.index("devm_add_action_or_reset(dev, battery_stop_poll"),
                        probe.index("battery_debugfs_init"))
        self.assertIn("gpiod_set_value(b->pull_low, 0)", SOURCE)

    def test_compiled_policy_tests(self):
        recipe = (ROOT / "recipes-kernel/linux/h432b-battery.inc").read_text()
        self.assertIn("${BUILD_CC}", recipe)
        self.assertIn("${B}/test-battery-policy", recipe)
        self.assertIn("-Wall -Wextra -Werror", recipe)
        self.assertIn('CONFIG_POWER_SUPPLY=y', recipe)
        config = (FILES / "u2-battery-test.config").read_text()
        self.assertNotIn("# CONFIG_SUSPEND is not set", config)
        self.assertIn("CONFIG_H432B_BATTERY=y", config)

if __name__ == "__main__":
    unittest.main()

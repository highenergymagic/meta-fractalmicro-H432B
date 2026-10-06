# SPDX-License-Identifier: MIT
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


class PowerInput(unittest.TestCase):
    def test_gpio_and_policy_boundary(self):
        dts = (ROOT / "recipes-kernel/linux/files/s5pv210-hims-u2.dts").read_text()
        node = dts.split("power-keys {", 1)[1].split("/* CE ROM", 1)[0]
        self.assertIn('compatible = "gpio-keys"', node)
        self.assertIn("gpios = <&gph2 6 GPIO_ACTIVE_HIGH>", node)
        self.assertIn("linux,code = <KEY_POWER>", node)
        self.assertNotIn("wakeup-source;", node)
        self.assertNotIn("autorepeat;", node)

    def test_input_built_in(self):
        config = (ROOT / "recipes-kernel/linux/files/u2-input.config").read_text()
        for key in ("INPUT", "INPUT_EVDEV", "INPUT_KEYBOARD", "KEYBOARD_GPIO"):
            self.assertIn("CONFIG_" + key + "=y", config)


if __name__ == "__main__":
    unittest.main()

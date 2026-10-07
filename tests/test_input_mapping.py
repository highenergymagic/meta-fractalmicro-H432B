# SPDX-License-Identifier: MIT
"""Source-contract checks, not substitutes for on-device evdev tests."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
FILES = ROOT / "recipes-kernel/linux/files"


class InputMapping(unittest.TestCase):
    def test_perkins_and_scroll_order(self):
        source = (FILES / "h432b-input.c").read_text()
        codes = re.search(r"matrix_codes\[15\] = \{(.*?)\};", source, re.S)[1]
        self.assertEqual(re.findall(r"(?:KEY|BTN)_[A-Z0-9_]+", codes), [
            "KEY_BACKSPACE", "KEY_BRL_DOT3", "KEY_BRL_DOT2", "KEY_BRL_DOT1",
            "KEY_SPACE", "KEY_BRL_DOT4", "KEY_BRL_DOT5", "KEY_BRL_DOT6",
            "KEY_ENTER", "KEY_F1", "KEY_F2", "KEY_F3", "KEY_F4",
            "BTN_TRIGGER_HAPPY1", "BTN_TRIGGER_HAPPY3"])
        self.assertIn("BTN_TRIGGER_HAPPY2, BTN_TRIGGER_HAPPY4", source)
        self.assertIn("s->row[i / 16] & BIT(i % 16)", source)

    def test_gpio_scope_and_polarity(self):
        text = (FILES / "s5pv210-hims-u2-input-test.dts").read_text()
        groups = {}
        for name, value in re.findall(r"(\w+)-gpios = (.*?);", text, re.S):
            groups[name] = re.findall(r"<&(\w+) (\d+) (GPIO_ACTIVE_\w+)>", value)
        self.assertEqual(groups["row"], [("gpj0", str(i), "GPIO_ACTIVE_LOW") for i in range(3)])
        self.assertEqual(groups["column"], [(bank, str(i), "GPIO_ACTIVE_LOW")
            for bank in ("gpj2", "gpj3") for i in range(8)])
        self.assertEqual(groups["selector"], [("gpj1", str(i), "GPIO_ACTIVE_HIGH") for i in range(4)])
        self.assertEqual(groups["direct"], [("gph2", str(i), "GPIO_ACTIVE_LOW")
            for i in range(1, 6)] + [("gph0", str(i), "GPIO_ACTIVE_LOW") for i in (5, 6)])

    def test_error_suspend_and_policy_boundary(self):
        source = (FILES / "h432b-input.c").read_text()
        for token in ("cancel_delayed_work_sync", "idle_rows(h)", "report_keys(h, &released)",
                      "devm_add_action_or_reset", "if (front)", "if (lock)"):
            self.assertIn(token, source)
        for forbidden in ("ioremap", "writel", "KEY_POWER", "EV_REP"):
            self.assertNotIn(forbidden, source)
        self.assertIn("module_platform_driver(h432_input_driver)", source)
        self.assertNotIn("module_platform_driver(driver)", source)
        self.assertNotIn("h432b-input.c", (ROOT / "recipes-kernel/linux/linux-h432b-base.inc").read_text())

    def test_motor_opt_in(self):
        source = (ROOT / "recipes-support/h432b-vibrator-test/files/vibrator-test.c").read_text()
        for token in ('"--pulse-100ms"', "100000000", 'req.offsets[0]=4',
                      'req.config.attrs[0].attr.values=0', "set(line,0)", "sigaction(SIGTERM"):
            self.assertIn(token, source)


if __name__ == "__main__":
    unittest.main()

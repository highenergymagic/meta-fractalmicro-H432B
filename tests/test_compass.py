# SPDX-License-Identifier: MIT
"""AMI603 integration contracts; these do not emulate the sensor."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
FILES = ROOT / "recipes-kernel/linux/files"

class Compass(unittest.TestCase):
    def test_dedicated_bus_and_no_wake(self):
        dt = (FILES / "s5pv210-hims-u2-compass.dtsi").read_text()
        for value in ("<&gpb 6 ", "<&gpb 4 ", "<&gpe1 2 ",
                      'compatible = "aichi,ami603"', "reg = <0x0f>"):
            self.assertIn(value, dt)
        self.assertNotIn("wakeup-source", dt)
        self.assertNotIn("interrupts =", dt)

    def test_identity_bounded_wait_and_power_cleanup(self):
        src = (FILES / "ami603.c").read_text()
        for value in ("AMI603_ID         0x45", "AMI603_WIA        0xba",
                      "i < 100", "-ETIMEDOUT", "devm_add_action_or_reset",
                      "ami603_suspend", "ami603_resume", "msleep(310)"):
            self.assertIn(value, src)
        self.assertNotIn("enable_irq_wake", src)
        self.assertNotIn("gpiod_", src)
        self.assertIn('devm_regulator_get(&client->dev, "vdd")', src)
        self.assertIn("ret = regulator_enable(s->vdd)", src)
        self.assertIn("ret = regulator_disable(s->vdd)", src)
        self.assertIn("ret = ami603_disable(s)", src)
        dt = (FILES / "s5pv210-hims-u2-compass.dtsi").read_text()
        self.assertIn("vdd-supply = <&compass_supply>", dt)
        self.assertIn('compatible = "regulator-fixed"', dt)
        include = (ROOT / "recipes-kernel/linux/h432b-compass.inc").read_text()
        self.assertIn("0026a-ami603-binding.patch", include)
        self.assertIn("REGULATOR_FIXED_VOLTAGE", include)

    def test_factory_parameters_read_only_and_page_restored(self):
        src = (FILES / "ami603.c").read_text()
        params = src.split("static int ami603_parameters")[1].split(
            "static int ami603_sample")[0]
        self.assertIn("ami603_write(s, AMI603_PAGE, 15)", params)
        self.assertIn("restore = ami603_write(s, AMI603_PAGE, 0)", params)
        for line in params.splitlines():
            if "ami603_write(" in line:
                self.assertIn("AMI603_PAGE", line)
        self.assertIn("(s16)get_unaligned_le16(data + 2 * axis)", src)
        self.assertIn("*val = 196133;", src)
        self.assertIn("10000 * (int)s->sensitivity", src)

    def test_standard_runtime_integration(self):
        recipe = (ROOT / "recipes-kernel/linux/linux-h432b-runtime_6.12.111.bb").read_text()
        self.assertIn("require h432b-compass.inc", recipe)
        self.assertIn("s5pv210-hims-u2-compass.dtsi",
                      (FILES / "s5pv210-hims-u2-runtime.dts").read_text())
        self.assertIn("CONFIG_AMI603=y",
                      (FILES / "h432b-compass.config").read_text())

# SPDX-License-Identifier: MIT
"""Runtime integration is part of a checkpoint, not only a diagnostic recipe."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
KERNEL = ROOT / "recipes-kernel/linux"
FILES = KERNEL / "files"


class RuntimePeripherals(unittest.TestCase):
    def test_shared_components_are_in_runtime(self):
        recipe = (KERNEL / "linux-h432b-runtime_6.12.111.bb").read_text()
        dt = (FILES / "s5pv210-hims-u2-runtime.dts").read_text()
        for name in ("input", "battery", "usb-host", "external-sd"):
            self.assertIn("require h432b-" + name + ".inc", recipe)
        for name in ("input", "battery", "external-sd"):
            self.assertIn('#include "s5pv210-hims-u2-' + name + '.dtsi"', dt)
        self.assertIn('#include "s5pv210-hims-u2-usb-host-test.dtsi"', dt)

    def test_no_experimental_sleep_or_charger_control(self):
        recipe = (KERNEL / "linux-h432b-runtime_6.12.111.bb").read_text()
        dt = (FILES / "s5pv210-hims-u2-runtime.dts").read_text()
        for forbidden in ("resume-test", "factory-resume", "wakeup-source", "regulator-min-microvolt"):
            self.assertNotIn(forbidden, recipe + dt)
        self.assertNotIn("# CONFIG_SUSPEND is not set",
                      (FILES / "u2-battery-test.config").read_text())

    def test_external_slot_remains_in_qualified_scope(self):
        dt = (FILES / "s5pv210-hims-u2-external-sd.dtsi").read_text()
        self.assertIn("hims,read-only-probe;", dt)
        self.assertIn("max-frequency = <25000000>", dt)
        self.assertNotIn("/delete-property/", dt)

    def test_diagnostics_use_the_same_wiring(self):
        for name in ("input", "battery", "external-sd"):
            diagnostic = (FILES / ("s5pv210-hims-u2-" + name + "-test.dts")).read_text()
            self.assertIn('#include "s5pv210-hims-u2-' + name + '.dtsi"', diagnostic)
            self.assertNotIn("-gpios =", diagnostic)

    def test_no_duplicate_patch_application(self):
        for name in ("input", "battery", "usb-host", "external-sd"):
            shared = (KERNEL / ("h432b-" + name + ".inc")).read_text()
            self.assertIn("SRC_URI +=", shared)
        resume = (KERNEL / "linux-h432b-resume-test_6.12.111.bb").read_text()
        self.assertNotIn("0011-usb-host-phy-lifecycle.patch", resume)
        self.assertNotIn("0012-onboard-h432b-usb-hub.patch", resume)


if __name__ == "__main__":
    unittest.main()

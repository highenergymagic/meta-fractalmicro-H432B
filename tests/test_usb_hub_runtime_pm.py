# SPDX-License-Identifier: MIT
"""H432B USB host topology and runtime-PM contracts."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
FILES = ROOT / "recipes-kernel/linux/files"


class HubRuntimePmTests(unittest.TestCase):
    def test_ehci_owns_external_hub(self):
        dts = (FILES / "s5pv210-hims-u2.dts").read_text()
        self.assertIn('&ohci { status = "disabled"; };', dts)
        self.assertIn('&ehci { status = "okay"; };', dts)
        hub = (FILES / "s5pv210-hims-u2-usb-host-test.dtsi").read_text()
        self.assertIn('&ehci {', hub)
        self.assertIn('compatible = "usb409,5a";', hub)
        self.assertNotIn('&ohci', hub)

    def test_standard_hub_power_management(self):
        inc = (ROOT / "recipes-kernel/linux/h432b-usb-host.inc").read_text()
        self.assertNotIn("0030-h432b-hub-runtime-pm.patch", inc)
        self.assertNotIn("0030-h432b-ohci-runtime-suspend.patch", inc)
        self.assertIn("0012-onboard-h432b-usb-hub.patch", inc)
        self.assertIn("0011-usb-host-phy-lifecycle.patch", inc)

    def test_obsolete_register_snapshots_removed(self):
        base = (ROOT / "recipes-kernel/linux/linux-h432b-base.inc").read_text()
        self.assertNotIn("0003-usb-host-diagnostics.patch", base)
        self.assertFalse((FILES / "0003-usb-host-diagnostics.patch").exists())


if __name__ == "__main__":
    unittest.main()

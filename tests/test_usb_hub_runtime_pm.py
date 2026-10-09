# SPDX-License-Identifier: MIT
"""H432B onboard hub runtime-PM contracts."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
FILES = ROOT / "recipes-kernel/linux/files"
PATCH = FILES / "0030-h432b-hub-runtime-pm.patch"


class HubRuntimePmTests(unittest.TestCase):
    def test_both_controllers_enabled(self):
        dts = (FILES / "s5pv210-hims-u2.dts").read_text()
        self.assertIn('&ohci { status = "okay"; };', dts)
        self.assertIn('&ehci { status = "okay"; };', dts)

    def test_workaround_scoped_to_board_and_onboard_hub(self):
        patch = PATCH.read_text()
        self.assertIn('of_machine_is_compatible("hims,braillesense-u2")', patch)
        self.assertIn('of_device_is_compatible(dev->of_node, "usb409,5a")', patch)
        self.assertIn("usb_disable_autosuspend(udev);", patch)
        self.assertNotIn("ohci-exynos.c", patch)
        self.assertNotIn("ehci-exynos.c", patch)
        self.assertNotIn("always_powered_in_suspend", patch)
        self.assertNotIn("device_wakeup_disable", patch)

    def test_obsolete_register_snapshots_removed(self):
        base = (ROOT / "recipes-kernel/linux/linux-h432b-base.inc").read_text()
        self.assertNotIn("0003-usb-host-diagnostics.patch", base)
        self.assertFalse((FILES / "0003-usb-host-diagnostics.patch").exists())

    def test_shared_profile_integration(self):
        inc = (ROOT / "recipes-kernel/linux/h432b-usb-host.inc").read_text()
        self.assertIn(PATCH.name, inc)
        self.assertNotIn("0030-h432b-ohci-runtime-suspend.patch", inc)


if __name__ == "__main__":
    unittest.main()

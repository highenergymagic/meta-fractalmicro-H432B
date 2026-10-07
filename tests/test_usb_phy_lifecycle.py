# SPDX-License-Identifier: MIT
"""Source guards for the opt-in host PHY lifecycle fix."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]

class UsbPhyLifecycleTests(unittest.TestCase):
    def test_each_host_balances_init_and_power(self):
        patch = (ROOT / "recipes-kernel/linux/files/0011-usb-host-phy-lifecycle.patch").read_text()
        for kind in ("ehci", "ohci"):
            part = patch.split("--- a/drivers/usb/host/" + kind + "-exynos.c", 1)[1]
            part = part.split("--- a/", 1)[0]
            added = "\n".join(line[1:] for line in part.splitlines()
                              if line.startswith("+") and not line.startswith("+++"))
            prefix = "exynos_" + kind
            self.assertLess(added.index("ret = phy_init("),
                            added.index("ret = phy_power_on("))
            self.assertIn("phy_exit(" + prefix + "->phy[i]);\n\t\t\tgoto rollback;", added)
            self.assertIn("while (--i >= 0)", added)
            self.assertEqual(added.count("phy_exit("), 3)
            self.assertEqual(added.count("phy_power_off("), 2)
            self.assertIn("for (i = PHY_NUMBER - 1; i >= 0; i--)", added)

    def test_experiment_only(self):
        recipe = ROOT / "recipes-kernel/linux"
        patch = "0011-usb-host-phy-lifecycle.patch"
        self.assertIn(patch, (recipe / "linux-h432b-resume-test_6.12.111.bb").read_text())
        self.assertNotIn(patch, (recipe / "linux-h432b_6.12.111.bb").read_text())

if __name__ == "__main__":
    unittest.main()

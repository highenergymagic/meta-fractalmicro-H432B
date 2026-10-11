# SPDX-License-Identifier: MIT
"""Power-off lifecycle/source contracts; hardware qualification is separate."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
FILES = ROOT / "recipes-kernel/linux/files"


class WakeablePoweroffTests(unittest.TestCase):
    def setUp(self):
        text = (FILES / "0031-h432b-wakeable-poweroff.patch").read_text()
        self.added = "\n".join(line[1:] for line in text.splitlines()
                               if line.startswith("+") and not line.startswith("+++"))

    def test_final_poweroff_not_suspend_lifecycle(self):
        self.assertIn("register_platform_power_off(h432b_power_off)", self.added)
        self.assertNotIn("pm_suspend(", self.added)
        self.assertNotIn("kernel_restart(", self.added)
        self.assertIn("cpu_suspend(0, s5pv210_cpu_suspend)", self.added)
        self.assertNotIn("i2c_transfer", self.added)

    def test_only_runtime_opts_in(self):
        self.assertIn("hims,wakeable-poweroff;", (FILES / "s5pv210-hims-u2-runtime.dts").read_text())
        self.assertNotIn("hims,wakeable-poweroff;", (FILES / "s5pv210-hims-u2-suspend.dtsi").read_text())
        self.assertIn('of_property_read_bool(of_root, "hims,wakeable-poweroff")', self.added)
        self.assertIn("!h432b_power_button_only", self.added)

    def test_release_before_wake_armed(self):
        release = self.added.index("while (released < 20)")
        arm = self.added.index("__raw_writel((u32)~BIT(H432B_POWER_EINT)")
        sleep = self.added.index("cpu_suspend(0, s5pv210_cpu_suspend)")
        self.assertLess(release, arm)
        self.assertLess(arm, sleep)
        self.assertIn("released = 0;", self.added)
        self.assertIn("mdelay(1);", self.added)
        self.assertIn("__raw_writel(~0U, S5P_WAKEUP_MASK)", self.added)
        self.assertIn("#define H432B_POWER_EINT\t\t22", self.added)

    def test_wake_is_stackless_hardware_restart(self):
        stub = self.added.split("ENTRY(h432b_poweroff_resume)", 1)[1]
        self.assertIn("0xe0102000", stub)
        self.assertIn("dsb\tsy", stub)
        self.assertNotIn("cpu_resume", stub)
        self.assertNotIn("\tsp", stub)
        self.assertIn("__pa_symbol(h432b_poweroff_resume)", self.added)
        self.assertIn("s5pv210_prepare_sleep(__pa_symbol(s5pv210_cpu_resume))", self.added)

    def test_recipe_order(self):
        recipe = (ROOT / "recipes-kernel/linux/h432b-suspend.inc").read_text()
        self.assertLess(recipe.index("0021-h432b-suspend-peripheral-retention.patch"),
                        recipe.index("0031-h432b-wakeable-poweroff.patch"))

    def test_cpu_interrupts_masked_before_sleep(self):
        self.assertIn("static void __iomem *h432b_vic[4]", self.added)
        self.assertIn("#define H432B_VIC_PHYS\t\t0xf2000000", self.added)
        self.assertIn("#define H432B_VIC_STRIDE\t\t0x100000", self.added)
        self.assertLess(self.added.index("writel(~0U, h432b_vic[bank] + H432B_VIC_ENABLE_CLEAR)"),
                        self.added.index("cpu_suspend(0, s5pv210_cpu_suspend)"))
        self.assertIn("writel(~0U, h432b_vic[bank] + H432B_VIC_SOFT_CLEAR)", self.added)
        self.assertIn("ioremap(H432B_VIC_PHYS +", self.added)


if __name__ == "__main__":
    unittest.main()

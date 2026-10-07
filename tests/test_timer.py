# SPDX-License-Identifier: MIT
from pathlib import Path
import unittest
ROOT = Path(__file__).resolve().parents[1]
BOOT = ROOT / "recipes-bsp/u-boot"

class TimerRecipe(unittest.TestCase):
    def test_isolated_ram_experiment(self):
        recipe = (BOOT / "u-boot-h432b-nand-timer_2012.10.bb").read_text()
        self.assertIn('H432B_UBOOT_ROLE = "ram-nand-timer"', recipe)
        self.assertIn("${BUILD_CC}", recipe)
        self.assertIn("test-timer", recipe)
        for name in ("u-boot-h432b.inc", "u-boot-h432b-chain.inc"):
            self.assertNotIn("timer/u2timer", (BOOT / name).read_text())

    def test_state_and_register_scope(self):
        text = (BOOT / "files/timer/u2timer.c").read_text()
        self.assertIn("gd->timer_rate_hz", text)
        self.assertIn("gd->tbu", text)
        self.assertNotIn("u2_soft_ticks", text)
        self.assertNotIn("dcache_enable", text)
        self.assertIn("con=readl(PWM(8))&~(START|UPDATE|RELOAD)", text)
        self.assertIn("i<1000000U", text)
        self.assertNotIn("writel(0,PWM(8))", text)

    def test_subpage_separate_and_corrected(self):
        recipe = (BOOT / "u-boot-h432b-nand-subpage_2012.10.bb").read_text()
        self.assertIn('H432B_UBOOT_ROLE = "ram-nand-subpage"', recipe)
        patch = (BOOT / "files/0007-bch-subpage-read.patch").read_text()
        self.assertIn("CONFIG_U2_NAND_SUBPAGE", patch)
        self.assertIn("NAND_ECC_SOFT_BCH", patch)
        self.assertNotIn("NAND_ECC_NONE", patch)
        driver = (BOOT / "files/nand/u2nand.c").read_text()
        self.assertIn("memcmp(full+offset,part,length)", driver)
        self.assertIn("mtd->ecc_stats.failed", driver)

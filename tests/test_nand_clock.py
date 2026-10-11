# SPDX-License-Identifier: MIT
"""NAND's required bus clock must be owned before register access."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
KERNEL = ROOT / "recipes-kernel/linux"
FILES = KERNEL / "files"


class NandClockTests(unittest.TestCase):
    def test_clock_patch_is_in_all_kernel_profiles(self):
        recipe = (KERNEL / "linux-h432b-base.inc").read_text()
        self.assertIn("file://0037-nand-clock-ownership.patch", recipe)
        self.assertLess(recipe.index("file://0015-nand-word-reads.patch"),
                        recipe.index("file://0037-nand-clock-ownership.patch"))

    def test_both_drivers_get_managed_clock_before_first_mmio(self):
        patch = (FILES / "0037-nand-clock-ownership.patch").read_text()
        for name in ("hims-u2-nand.c", "hims-u2-nand-ro.c"):
            start = patch.index("+++ b/drivers/mtd/nand/raw/" + name + "\n")
            source = patch[start:].split("\ndiff --git ", 1)[0]
            self.assertIn("+#include <linux/clk.h>", source)
            self.assertIn('+\tclk = devm_clk_get_enabled(dev, "nand");', source)
            self.assertIn('+\tclk = devm_clk_get_enabled(dev, "bus");', source)
            self.assertLess(source.index('devm_clk_get_enabled(dev, "bus")'),
                            source.index("readl(n->regs + NFCONT)"))
            self.assertIn("+\tif (IS_ERR(clk))", source)
            self.assertIn("+\t\treturn dev_err_probe(dev, PTR_ERR(clk),", source)
            self.assertLess(source.index("devm_clk_get_enabled"),
                            source.index("readl(n->regs + NFCONT)"))
            self.assertNotIn("get_optional", source)
            self.assertNotIn("clk_disable", source)

    def test_dt_owns_both_board_qualified_nand_gates(self):
        tree = (FILES / "s5pv210-hims-u2.dts").read_text()
        node = re.search(r"nand@b0e00000\s*\{([^}]+)\}", tree).group(1)
        self.assertIn('compatible = "hims,h432b-nand";', node)
        self.assertIn("clocks = <&clocks CLK_NFCON>, <&clocks CLK_NANDXL>;", node)
        self.assertIn('clock-names = "nand", "bus";', node)


if __name__ == "__main__":
    unittest.main()

# SPDX-License-Identifier: MIT
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]

class ToolchainContract(unittest.TestCase):
    def test_reconfigure_drops_stale_compiler_dependencies(self):
        recipe = (ROOT / "recipes-bsp/u-boot/u-boot-h432b.inc").read_text()
        self.assertLess(recipe.index("oe_runmake tidy"),
                        recipe.index("oe_runmake hims_u2_config"))
        # mrproper/clobber would remove the embedded stage installed in prepend.
        self.assertNotIn("oe_runmake mrproper", recipe)
        self.assertNotIn("oe_runmake clobber", recipe)

if __name__ == "__main__":
    unittest.main()

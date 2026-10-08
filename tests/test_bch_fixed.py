# SPDX-License-Identifier: MIT
"""Keep fixed BCH parameters scoped to the matching NAND geometry."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1] / "recipes-bsp/u-boot"

class FixedBCH(unittest.TestCase):
    def test_parameters_and_build_gate(self):
        recipe = (ROOT / "u-boot-h432b-ab_2012.10.bb").read_text()
        for setting in ("CONFIG_BCH_CONST_PARAMS 1", "CONFIG_BCH_CONST_M 13",
                        "CONFIG_BCH_CONST_T 8"):
            self.assertIn(setting, recipe)
        self.assertIn('check-bch-equivalence.py --source ${S} --cc "${BUILD_CC}"', recipe)
        nand = (ROOT / "files/nand/u2nand.c").read_text()
        self.assertIn("n->ecc.size=512; n->ecc.bytes=13;", nand)
        self.assertEqual((1 + 8 * 512).bit_length(), 13)
        self.assertEqual(13 * 8 // 13, 8)

    def test_equivalence_uses_fetched_source(self):
        script = (ROOT / "files/nand/check-bch-equivalence.py").read_text()
        self.assertIn('args.source / "lib/bch.c"', script)
        self.assertIn('for fixed in (False, True)', script)
        self.assertIn('"-DCONFIG_BCH_CONST_M=13"', script)
        self.assertIn('"-DCONFIG_BCH_CONST_T=8"', script)
        self.assertNotIn("-DNDEBUG", script)

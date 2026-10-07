# SPDX-License-Identifier: MIT
from pathlib import Path
import unittest
ROOT = Path(__file__).resolve().parents[1] / "recipes-bsp/u-boot"
class MaintenanceChain(unittest.TestCase):
    def test_shared_chain_requires_explicit_stage(self):
        text = (ROOT / "u-boot-h432b-chain.inc").read_text()
        self.assertNotIn('H432B_CHAIN_STAGE_RECIPE ?=', text)
        self.assertIn('${H432B_CHAIN_STAGE_RECIPE}:do_deploy', text)
        self.assertIn('check-memory.py', text)
        self.assertIn('--chain', text)

    def test_explicit_variant(self):
        text = (ROOT / "u-boot-h432b-maintenance-chain_2012.10.bb").read_text()
        self.assertIn('H432B_CHAIN_STAGE_RECIPE = "u-boot-h432b-maintenance"', text)
        self.assertIn('H432B_CHAIN_STAGE_DIR = "ram-maintenance-b"', text)
        self.assertIn('H432B_CHAIN_CARRIER_DIR = "nand-maintenance-ce-carrier"', text)

    def test_b_only_changes_boot_selection(self):
        text = (ROOT / "files/0009-maintenance-kernel-b.patch").read_text()
        removed = [x[1:] for x in text.splitlines() if x.startswith("-#define")]
        added = [x[1:] for x in text.splitlines() if x.startswith("+#define")]
        self.assertEqual(len(removed), 1)
        self.assertEqual(added, [removed[0].replace("u2nandboot a", "u2nandboot b")])

if __name__ == "__main__":
    unittest.main()

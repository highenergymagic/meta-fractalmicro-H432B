# SPDX-License-Identifier: MIT
"""Offline build and address-scope checks for the clock inventory utility."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "recipes-support/h432b-clock-inventory/files/clock-inventory.c"


class ClockInventoryTests(unittest.TestCase):
    def test_native_build_and_explicit_invocation(self):
        compiler = os.environ.get("POWER_NATIVE_CC") or shutil.which("cc")
        self.assertTrue(compiler)
        with tempfile.TemporaryDirectory() as directory:
            binary = str(Path(directory) / "inventory")
            subprocess.run([compiler, "-D_FILE_OFFSET_BITS=64", "-std=c11",
                            "-Wall", "-Wextra", "-Werror", str(SOURCE),
                            "-o", binary], check=True)
            for arguments in ([], ["--write"], ["--read", "extra"]):
                result = subprocess.run([binary, *arguments], capture_output=True)
                self.assertEqual(result.returncode, 2)
                self.assertIn(b"Usage:", result.stderr)

    def test_fixed_read_only_scope(self):
        text = SOURCE.read_text()
        self.assertIn('hims,braillesense-u2', text)
        self.assertIn('PROT_READ, MAP_SHARED', text)
        self.assertNotIn('PROT_WRITE', text)
        self.assertNotIn('O_RDWR', text)
        self.assertIn('const volatile uint32_t *reg', text)
        for address in ("0xe0200000", "0xe0100000", "0xf0000000", "0xf1400000"):
            self.assertIn(address, text)
        self.assertNotIn("strtoul", text)  # No arbitrary user-selected MMIO aperture.

    def test_pinned_recipe(self):
        recipe = (SOURCE.parent.parent / "h432b-clock-inventory_1.0.bb").read_text()
        self.assertIn('${CC}', recipe)
        self.assertIn('-D_FILE_OFFSET_BITS=64', recipe)
        self.assertIn('COMPATIBLE_MACHINE = "^h432b$"', recipe)


if __name__ == "__main__":
    unittest.main()

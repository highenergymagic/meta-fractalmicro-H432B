# SPDX-License-Identifier: MIT
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "recipes-bsp/u-boot/files/bootstate"

class BootstateModel(unittest.TestCase):
    def test_programming_uses_hardware_timeouts(self):
        recipe = ROOT / "recipes-bsp/u-boot/u-boot-h432b-bootstate_2012.10.bb"
        self.assertIn("require u-boot-h432b-nand-timer_2012.10.bb", recipe.read_text())
        timer = ROOT / "recipes-bsp/u-boot/u-boot-h432b-nand-timer_2012.10.bb"
        self.assertIn("${S}/arch/arm/cpu/armv7/s5p-common/timer.c", timer.read_text())

    def test_compiled_fault_model(self):
        compiler = os.environ.get("BOOTSTATE_NATIVE_CC", "cc")
        if not shutil.which(compiler):
            self.skipTest("native C compiler unavailable")
        with tempfile.TemporaryDirectory() as folder:
            program = str(Path(folder) / "bootstate")
            subprocess.run([compiler, "-std=c99", "-O2", "-Wall", "-Wextra",
                            "-Werror", str(SOURCE / "test-bootstate.c"), "-o", program],
                           check=True)
            result = subprocess.run([program], check=True, text=True, capture_output=True)
            self.assertIn("BOOTSTATE_MODEL_PASS", result.stdout)

if __name__ == "__main__":
    unittest.main()

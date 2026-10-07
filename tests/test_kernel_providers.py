# SPDX-License-Identifier: MIT
"""A single runtime provider and isolated optional diagnostic kernels."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
RECIPES = ROOT / "recipes-kernel/linux"


class KernelProviderTests(unittest.TestCase):
    def test_shared_base_is_not_a_selectable_kernel(self):
        recipe = (RECIPES / "linux-h432b-base.inc").read_text()
        self.assertIn('H432B_KERNEL_PROVIDER_REMOVE ?= "virtual/kernel"', recipe)
        self.assertIn('PROVIDES:remove = "${H432B_KERNEL_PROVIDER_REMOVE}"', recipe)
        self.assertIn('KERNEL_DEPLOYSUBDIR = "${KERNEL_PACKAGE_NAME}"', recipe)
        self.assertFalse((RECIPES / "linux-h432b_6.12.111.bb").exists())
        self.assertFalse((RECIPES / "linux-h432b-reboot-test_6.12.111.bb").exists())

    def test_runtime_restores_standard_package_and_provider(self):
        recipe = (RECIPES / "linux-h432b-runtime_6.12.111.bb").read_text()
        self.assertIn('H432B_KERNEL_PROVIDER_REMOVE = ""', recipe)
        self.assertIn('KERNEL_PACKAGE_NAME = "kernel"', recipe)
        self.assertIn('KERNEL_DEPLOYSUBDIR = "kernel-runtime"', recipe)


if __name__ == "__main__":
    unittest.main()

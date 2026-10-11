# SPDX-License-Identifier: MIT
"""Regression checks for runtime capability and diagnostic boundaries."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
KERNEL = ROOT / "recipes-kernel/linux"
FILES = KERNEL / "files"


class KernelIntegrationAuditTests(unittest.TestCase):
    def test_normal_runtime_releases_all_sd_write_guards(self):
        text = (FILES / "s5pv210-hims-u2-runtime.dts").read_text()
        for controller in ("sdhci1", "sdhci2", "sdhci3"):
            block = re.search(r"&" + controller + r"\s*\{([^}]+)\}", text)
            self.assertIsNotNone(block)
            self.assertIn("/delete-property/ hims,read-only-probe;", block[1])
        self.assertIn("hims,write-window = <0x00400000 0x1fb00000>", text)

    def test_runtime_firewall_support_and_no_pm_test_mode(self):
        text = (FILES / "u2-systemd.config").read_text()
        for option in ("BPF_SYSCALL", "BPF_JIT", "CGROUP_BPF"):
            self.assertIn("CONFIG_" + option + "=y", text)
        config = (FILES / "h432b-runtime.config").read_text()
        for option in ("PM_DEBUG", "PM_SLEEP_DEBUG", "PM_TEST_SUSPEND"):
            self.assertIn("# CONFIG_" + option + " is not set", config)
        recipe = (KERNEL / "linux-h432b-runtime_6.12.111.bb").read_text()
        self.assertGreater(recipe.index("${UNPACKDIR}/h432b-runtime.config"),
                           recipe.index("require h432b-suspend.inc"))

    def test_dwc2_defaults_follow_hardware_not_board_name(self):
        patch = (FILES / "0034-dwc2-hardware-defaults.patch").read_text()
        self.assertIn("snpsid >= DWC2_CORE_REV_3_00a", patch)
        self.assertIn("min_t(u32, 2048, hw->rx_fifo_size)", patch)
        self.assertIn("min_t(u32, 1024,", patch)
        self.assertNotIn("of_machine_is_compatible", patch)
        self.assertNotIn("-\t\tCHECK_", patch)

    def test_scalar_aes_acceleration_is_board_policy(self):
        config = (FILES / "h432b-runtime.config").read_text()
        self.assertIn("CONFIG_CRYPTO_AES_ARM=y", config)
        radio = (FILES / "0016-wifi-transport.patch").read_text()
        self.assertNotIn("select CRYPTO_AES_ARM", radio)
        self.assertNotIn("select KERNEL_MODE_NEON", radio)

    def test_normal_runtime_does_not_bypass_clock_ownership(self):
        runtime = (FILES / "s5pv210-hims-u2-runtime.dts").read_text()
        self.assertNotIn("clk_ignore_unused", runtime)

    def test_detailed_loader_profile_is_explicit(self):
        recipe = (ROOT / "recipes-bsp/u-boot/u-boot-h432b-ab_2012.10.bb").read_text()
        self.assertIn('H432B_NAND_TIMING ?= "0"', recipe)
        self.assertIn('case "${H432B_NAND_TIMING}" in', recipe)
        self.assertIn('H432B_NAND_TIMING must be 0 or 1', recipe)


if __name__ == "__main__":
    unittest.main()

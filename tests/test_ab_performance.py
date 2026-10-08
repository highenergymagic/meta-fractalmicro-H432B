# SPDX-License-Identifier: MIT
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1] / "recipes-bsp/u-boot"
FILES = ROOT / "files"

class ABPerformance(unittest.TestCase):
    def test_default_stage_enables_qualified_read_optimizations(self):
        recipe = (ROOT / "u-boot-h432b-ab_2012.10.bb").read_text()
        self.assertIn("file://0007-bch-subpage-read.patch", recipe)
        self.assertIn("#define CONFIG_U2_NAND_SUBPAGE 1", recipe)
        self.assertIn("#define CONFIG_H432B_REUSE_UBI 1", recipe)
        autoboot = (FILES / "bootstate/ab-autoboot.patch").read_text()
        self.assertIn("u2bootmode && u2icache on && u2nandinit", autoboot)
        self.assertNotIn("dcache on", autoboot)

    def test_reuse_requires_live_named_partition(self):
        patch = (FILES / "bootstate/reuse-ubi-attach.patch").read_text()
        self.assertIn("ubi_initialized && ubi && ubi_dev.selected", patch)
        self.assertIn('!strcmp(ubi_dev.part_name, "linux")', patch)
        self.assertIn('return run_command("ubi part linux", 0)', patch)
        for name in ("nand/u2nand.c", "bootstate/u2bootstate.c"):
            source = (FILES / name).read_text()
            self.assertIn("#ifdef CONFIG_H432B_REUSE_UBI", source)
            self.assertIn("h432b_ubi_attach_linux()", source)

    def test_timing_uses_hardware_clock_and_preserves_attempt(self):
        source = (FILES / "bootstate/u2bootstate.c").read_text()
        for field in ("attach", "kernel", "total"):
            self.assertIn("openh432.loader_" + field + "_ms=%lu", source)
        self.assertIn("get_timer(kernel_started)", source)
        self.assertIn("get_timer(boot_started)", source)
        self.assertIn("openh432.attempt=%u", source)
        self.assertIn("h432b_bootstate_attempt(&store, scratch)", source)

    def test_correction_and_image_checks_remain(self):
        patch = (FILES / "0007-bch-subpage-read.patch").read_text()
        self.assertIn("NAND_ECC_SOFT_BCH", patch)
        source = (FILES / "nand/u2nand.c").read_text()
        self.assertIn("ubi_volume_read_bounded", source)
        self.assertIn("u2_fb_layout", source)
        self.assertIn("crc32(0,", source)

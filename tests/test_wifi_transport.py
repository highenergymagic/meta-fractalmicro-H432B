# SPDX-License-Identifier: MIT
"""Offline transport-test scope contracts; not SDIO hardware qualification."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
KERNEL = ROOT / "recipes-kernel/linux"
SOURCE = (KERNEL / "files/h432b-wifi-transport.c").read_text()


class WifiTransport(unittest.TestCase):
    def test_explicit_board_and_function_match(self):
        self.assertIn("SDIO_DEVICE(0x024c, 0x8712)", SOURCE)
        self.assertIn("func->num != 1", SOURCE)
        self.assertIn('of_machine_is_compatible("hims,braillesense-u2")', SOURCE)

    def test_bind_performs_no_io(self):
        probe = SOURCE.split("static int h432b_wifi_probe", 1)[1]
        probe = probe.split("static const struct sdio_device_id", 1)[0]
        for forbidden in ("sdio_read", "sdio_write", "sdio_enable_func", "take_sample"):
            self.assertNotIn(forbidden, probe)
        self.assertIn("device_add_group", probe)

    def test_one_explicit_sample(self):
        self.assertIn("DEVICE_ATTR_WO(sample)", SOURCE)
        self.assertIn('sysfs_streq(buf, "1")', SOURCE)
        self.assertIn("return -EALREADY", SOURCE)
        self.assertIn("mutex_lock(&sample->lock)", SOURCE)

    def test_small_local_window_and_heap_buffer(self):
        self.assertIn("#define SAMPLE_BYTES 4", SOURCE)
        self.assertIn("devm_kzalloc", SOURCE)
        self.assertIn("sdio_memcpy_fromio(func, sample->bulk, 0, SAMPLE_BYTES)", SOURCE)
        for forbidden in ("sdio_writeb(", "sdio_writel(", "sdio_memcpy_toio(",
                          "sdio_claim_irq(", "request_firmware(", "alloc_netdev"):
            self.assertNotIn(forbidden, SOURCE)

    def test_restore_and_report_errors(self):
        self.assertIn("sdio_f0_readb(func, SDIO_CCCR_IOEx", SOURCE)
        self.assertIn("sdio_set_block_size(func, saved_blksize)", SOURCE)
        self.assertIn("if (enable_attempted)", SOURCE)
        self.assertIn("sdio_disable_func(func)", SOURCE)
        self.assertEqual(SOURCE.count("sdio_claim_host(func)"), 2)
        self.assertEqual(SOURCE.count("sdio_release_host(func)"), 2)
        self.assertIn("sample->cleanup_error = cleanup", SOURCE)

    def test_remove_drains_sysfs_before_managed_free(self):
        self.assertIn(".remove = h432b_wifi_remove", SOURCE)
        self.assertIn("device_remove_group(&func->dev, &h432b_wifi_group)", SOURCE)

    def test_separate_recipe_only(self):
        recipe = (KERNEL / "linux-h432b-wifi-test_6.12.111.bb").read_text()
        self.assertIn("require linux-h432b-runtime_6.12.111.bb", recipe)
        self.assertIn('H432B_KERNEL_PROVIDER_REMOVE = "virtual/kernel"', recipe)
        self.assertIn('KERNEL_DEPLOYSUBDIR = "kernel-wifi-test"', recipe)
        runtime = (KERNEL / "linux-h432b-runtime_6.12.111.bb").read_text()
        self.assertNotIn("wifi-transport", runtime)


if __name__ == "__main__":
    unittest.main()

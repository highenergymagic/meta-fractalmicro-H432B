# SPDX-License-Identifier: MIT
"""Power-sequence source contracts, not live qualification."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1] / "recipes-kernel/linux"
POWER = (ROOT / "files/h432b-wifi-power.h").read_text()
DRIVER = (ROOT / "files/h432b-wifi-transport.c").read_text()
DRIVER += (ROOT / "files/h432b-wifi-debug.h").read_text()


class WifiPower(unittest.TestCase):
    def test_explicit_request_after_transport(self):
        self.assertIn("DEVICE_ATTR_WO(power_init)", DRIVER)
        self.assertIn("if (r->attempted)", DRIVER)
        self.assertIn("!sample->attempted || sample->error || sample->cleanup_error", DRIVER)
        self.assertIn("return -EAGAIN", DRIVER)

    def test_wlan_and_local_windows_are_distinct(self):
        self.assertIn("0x8000 | offset", POWER)
        self.assertIn("sdio_writeb(func, 0, 0x80, &error)", POWER)
        self.assertNotIn("sdio_claim_irq", POWER + DRIVER)
        self.assertNotIn("request_firmware", POWER)
        self.assertIn("sdio_memcpy_toio(func, address, func->tmpbuf, 1)", POWER)
        self.assertIn("sdio_memcpy_fromio(func, func->tmpbuf, address, 1)", POWER)

    def test_power_register_allowlist(self):
        offsets = {int(x, 16) for x in re.findall(r"P(?:8|16|32)\((0x[0-9a-f]+),", POWER)}
        self.assertEqual(offsets, {
            0x00, 0x02, 0x03, 0x08, 0x09, 0x10, 0x11, 0x1f,
            0x20, 0x21, 0x26, 0x28, 0x34, 0x40, 0x42, 0x50,
        })
        self.assertNotIn("0x30,", POWER)  # no efuse programming command

    def test_timing_and_warm_signature(self):
        self.assertEqual(re.findall(r"WAIT_US\((\d+)\)", POWER),
                         ["1500", "500", "1000", "100", "500"])
        for value in ("0x6911", "0xdb8f", "0xb8a4"):
            self.assertIn(value, POWER)
        self.assertIn("i < 1001", POWER)
        self.assertIn("return -ETIMEDOUT", POWER)

    def test_switch_regulator_word_not_ldo_word(self):
        self.assertIn("P16(0x11, 0xffff, 0x1001)", POWER)
        self.assertIn("wifi_write(func, 2, 0x11, 0x5497", POWER)
        self.assertNotIn("wifi_write(func, 2, 0x20, 0x5497", POWER)

    def test_end_to_end_readback(self):
        self.assertIn("r->command != 0x3fff || r->verify != 0x5678", POWER)
        self.assertIn("wifi_signature(func, r->before)", POWER)
        self.assertIn("wifi_signature(func, r->after)", POWER)
        self.assertIn("r->cleanup = sdio_disable_func(func)", DRIVER)

    def test_optional_recipe_carries_header(self):
        recipe = (ROOT / "h432b-wifi.inc").read_text()
        self.assertIn("file://h432b-wifi-power.h", recipe)
        self.assertIn("${UNPACKDIR}/h432b-wifi-power.h", recipe)


if __name__ == "__main__":
    unittest.main()

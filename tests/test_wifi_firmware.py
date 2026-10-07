# SPDX-License-Identifier: MIT
"""Firmware loader scope and bounds contracts; not a hardware emulator."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1] / "recipes-kernel/linux"
FW = (ROOT / "files/h432b-wifi-firmware.h").read_text()
DRIVER = (ROOT / "files/h432b-wifi-transport.c").read_text()


class WifiFirmware(unittest.TestCase):
    def test_size_bounds_before_sum(self):
        self.assertIn("fw->size < 80 || fw->size > 200000", FW)
        self.assertIn("*imem > 65536 || *emem > 65536 || dmem > 65536", FW)
        self.assertIn("(size_t)80 + *imem + *emem + dmem != fw->size", FW)
        self.assertIn("get_unaligned_le32(fw->data + 16) != 48", FW)

    def test_padded_incrementing_fifo_packets(self):
        self.assertIn("#define WIFI_FW_CHUNK 49152", FW)
        self.assertIn("#define WIFI_FW_FIFO 0x18d40", FW)
        self.assertIn("transfer = ALIGN(length + 32, 512)", FW)
        self.assertIn("memset(packet, 0, transfer)", FW)
        self.assertIn("length == remaining ? BIT(28) : 0", FW)
        self.assertIn("sdio_memcpy_toio(func, WIFI_FW_FIFO, packet, transfer)", FW)
        self.assertNotIn("sdio_writesb", FW)
        self.assertIn("host->max_blk_count < WIFI_FW_PACKET / 512", FW)

    def test_completion_and_stale_flags(self):
        self.assertIn("r->initial & 0x35", FW)
        for fragment in ("BIT(0), BIT(1)", "BIT(2), BIT(3)", "BIT(5), BIT(5)",
                         "BIT(4), BIT(4)", "BIT(7), BIT(7)"):
            self.assertIn(fragment, FW)
        self.assertIn("? 0 : -EBADMSG", FW)
        self.assertIn("return -ETIMEDOUT", FW)
        self.assertIn("r->stage = 8", FW)
        self.assertEqual(0x000a & 0x35, 0)  # measured reset checksum flags
        for done in (1, 4, 16, 32):
            self.assertNotEqual(done & 0x35, 0)

    def test_explicit_request_and_private_external_firmware(self):
        self.assertIn('sysfs_streq(buf, "memory")', DRIVER)
        self.assertIn("request_firmware_direct", DRIVER)
        self.assertIn("release_firmware(fw)", DRIVER)
        self.assertIn("!sample->power.attempted || sample->power.error", DRIVER)
        self.assertNotIn("request_firmware_nowait", DRIVER)
        self.assertIn('WIFI_FW_NAME "h432b/rtl8712s.bin"', FW)

    def test_board_configuration_and_full_ready(self):
        for fragment in ("memset(config, 0, 48)", "config[2] = 0x14",
                         "config[6] = 0x12", "config[0x0e] = 1",
                         "config[0x12] = 2", "config[0x13] = 2",
                         "config[0x19] = 1", "packet, config, sizeof(config)",
                         "60 : 30, 100000", "i <= tries", "if (i != tries)"):
            self.assertIn(fragment, FW)
        self.assertIn('sysfs_streq(buf, "full")', DRIVER)

    def test_restore_and_no_network_interface(self):
        self.assertIn("sdio_set_block_size(func, saved_blksize)", FW)
        self.assertIn("sdio_disable_func(func)", FW)
        self.assertEqual(FW.count("sdio_claim_host(func)"), 1)
        self.assertEqual(FW.count("sdio_release_host(func)"), 1)
        self.assertIn("if (!full)", FW)
        self.assertIn("r->stage = 12", FW)
        self.assertNotIn("sdio_claim_irq", FW + DRIVER)
        self.assertNotIn("alloc_netdev", FW + DRIVER)


if __name__ == "__main__":
    unittest.main()

# SPDX-License-Identifier: MIT
"""Offline FIFO bounds and diagnostic-scope checks."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1] / "recipes-kernel/linux/files"
EVENT = (ROOT / "h432b-wifi-events.h").read_text()
FW = (ROOT / "h432b-wifi-firmware.h").read_text()
DRIVER = (ROOT / "h432b-wifi-transport.c").read_text()


class WifiEvents(unittest.TestCase):
    def test_counter_baseline_precedes_upload(self):
        self.assertLess(FW.index("r->c2h_base = sdio_readw"),
                        FW.index("error = wifi_fw_section"))
        self.assertIn("pending = (u16)(r->blocks - baseline)", EVENT)
        self.assertIn("pending > WIFI_EVENT_MAX / 512", EVENT)
        self.assertIn("error = -ENODATA", EVENT)

    def test_bounded_single_first_fifo_read(self):
        self.assertIn("#define WIFI_EVENT_MAX 16384", EVENT)
        self.assertIn("#define WIFI_C2H_FIFO 0x18e80", EVENT)
        self.assertEqual(EVENT.count("mmc_io_rw_extended("), 1)
        self.assertIn("data, pending, 512", EVENT)
        self.assertNotIn("sdio_memcpy_fromio", EVENT)
        self.assertIn("host->max_blk_count < WIFI_EVENT_MAX / 512", EVENT)
        self.assertIn("sample->power.warm", DRIVER)

    def test_packet_and_event_bounds(self):
        for fragment in ("size - offset < 32", "& 0x1ff) != 0x1ff",
                         "packet < 8", "packet > size - offset - 24",
                         "payload > packet - 8", "stride > size - offset",
                         "offset += stride"):
            self.assertIn(fragment, EVENT)
        self.assertIn("ALIGN(packet + 24, 512)", EVENT)

    def test_no_interrupt_or_command_writes(self):
        self.assertIn("if (mask || func->irq_handler)", EVENT)
        self.assertNotIn("sdio_write", EVENT)
        self.assertNotIn("sdio_claim_irq", EVENT)
        self.assertIn("sdio_set_block_size(func, saved)", EVENT)
        self.assertIn("sdio_disable_func(func)", EVENT)
        self.assertIn("kfree(data)", EVENT)


if __name__ == "__main__":
    unittest.main()

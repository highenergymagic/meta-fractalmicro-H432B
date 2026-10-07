# SPDX-License-Identifier: MIT
"""Offline command format and bounded stream contracts."""
from pathlib import Path
import unittest
ROOT = Path(__file__).resolve().parents[1] / "recipes-kernel/linux/files"
C = (ROOT / "h432b-wifi-command.h").read_text()
D = (ROOT / "h432b-wifi-transport.c").read_text()

class WifiCommand(unittest.TestCase):
    def test_command_descriptor(self):
        for text in ("memset(packet, 0, 512)", "0x8c200028", "0x1300",
                     "0x002c0020 | ((u32)seq << 24)", "packet + 40", "packet, 1, 512"):
            self.assertIn(text, C)
        self.assertIn("mmc_io_rw_extended(func->card, 1, func->num, 0x18c80, 1", C)

    def test_factory_cmd53_argument(self):
        factory = (((0x18c80 | (1 << 19) | 0xffc60000) << 9) & 0xffffffff) | 1
        linux = (1 << 31) | (1 << 28) | (1 << 27) | (1 << 26) | (0x18c80 << 9) | 1
        self.assertEqual(factory, linux)
        self.assertEqual(linux, 0x9f190001)
        self.assertNotIn("  packet, 0, 512);", C)

    def test_sequence_and_consumed_count(self):
        for text in ("WIFI_C2H_FIFO | (r->port_seq & 3)",
                     "r->port_seq = (r->port_seq + 1) & 3",
                     "r->consumed = count", "seq != r->event_seq",
                     "r->event_seq = (r->event_seq + 1) & 0x7f"):
            self.assertIn(text, C)
        self.assertIn("wifi_event_parse(data, pending * 512, &parsed)", C)

    def test_reply_fields_and_bounds(self):
        for text in ("length == 12", "p[9] == 0x11 + seq * 0x10",
                     "r->command_seq <= 2", "r->replies++", "r->batches >= 64",
                     "i < 32", "i < 100 && !r->matched"):
            self.assertIn(text, C)
        self.assertIn("!r->sent || r->matched", C)

    def test_wait_and_cleanup(self):
        self.assertIn("sdio_release_host(func);\n\t\t\tmsleep(20);\n\t\t\tsdio_claim_host(func)", C)
        self.assertIn("sdio_set_block_size(func, saved)", C)
        self.assertIn("sdio_disable_func(func)", C)
        self.assertIn("kfree(data)", C)

    def test_queue_snapshots_are_bounded_and_read_only(self):
        snapshot = C.split("static void wifi_command_snapshot", 1)[1].split("static void wifi_loopback_packet", 1)[0]
        for text in ("ARRAY_SIZE(r->snapshot)", "0x40", "WIFI_C2H_COUNT",
                     "0xc0 + i", "if (s->error)", "sdio_readw", "sdio_readb"):
            self.assertIn(text, snapshot)
        self.assertNotIn("sdio_write", snapshot)
        self.assertIn("snapshot[5]", C)
        self.assertIn("r->command_seq * 2 - 1", C)
        self.assertIn("r->command_seq * 2);", C)
        self.assertIn("sysfs_emit_at", D)

    def test_exclusive_stream_owner(self):
        self.assertIn("r->attempted || sample->event.attempted", D)
        self.assertIn("r->attempted || sample->command.attempted", D)
        self.assertIn("!sample->ack.attempted || sample->ack.error", D)

if __name__ == "__main__":
    unittest.main()

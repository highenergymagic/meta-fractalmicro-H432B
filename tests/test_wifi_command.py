# SPDX-License-Identifier: MIT
"""Offline command format and bounded stream contracts."""
from pathlib import Path
import unittest
ROOT = Path(__file__).resolve().parents[1] / "recipes-kernel/linux/files"
C = (ROOT / "h432b-wifi-command.h").read_text()
D = (ROOT / "h432b-wifi-transport.c").read_text()
D += (ROOT / "h432b-wifi-debug.h").read_text()

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

    def test_c2h_single_block_is_not_byte_mode(self):
        self.assertIn("mmc_io_rw_extended(func->card, 0, func->num", C)
        self.assertIn("data, pending, 512", C)
        self.assertNotIn("sdio_memcpy_fromio", C)
        self.assertEqual((1 << 28) | (1 << 27) | (1 << 26) |
                         (0x18e80 << 9) | 1, 0x1f1d0001)

    def test_reply_fields_and_bounds(self):
        for text in ("length == 12", "p[9] == 0x11 + seq * 0x10",
                     "r->survey ? 3 : (r->opmode ? 16 : 2)", "r->replies++", "r->batches >= (r->stress ? 512 : (r->survey ? 256 : 64))",
                     "i < 32", "i < tries && !r->matched"):
            self.assertIn(text, C)
        self.assertIn("!r->sent || r->matched", C)

    def test_wait_and_cleanup(self):
        self.assertIn("sdio_release_host(func);\n\t\t\tmsleep(20);\n\t\t\tsdio_claim_host(func)", C)
        self.assertIn("sdio_set_block_size(func, saved)", C)
        self.assertIn("sdio_disable_func(func)", C)
        self.assertIn("kfree(data)", C)

    def test_queue_snapshots_are_bounded_and_read_only(self):
        snapshot = C.split("static void wifi_command_snapshot", 1)[1].split("static int wifi_command_mac_init", 1)[0]
        for text in ("ARRAY_SIZE(r->snapshot)", "0x40", "WIFI_C2H_COUNT",
                     "0xc0 + i", "if (s->error)", "sdio_readw", "sdio_readb"):
            self.assertIn(text, snapshot)
        self.assertNotIn("sdio_write", snapshot)
        self.assertIn("snapshot[5]", C)
        self.assertIn("r->command_seq * 2 - 1", C)
        self.assertIn("r->command_seq * 2);", C)
        self.assertIn("sysfs_emit_at", D)

    def test_factory_post_firmware_sequence(self):
        init = C.split("static int wifi_command_mac_init", 1)[1].split("static void wifi_loopback_packet", 1)[0]
        writes = ["wifi_write(func, 4, 0x48", "wifi_write(func, 4, 0x40",
                  "wifi_write(func, 1, 0x06, 0x3b", "wifi_write(func, 1, 0x40, 0xfc",
                  "wifi_write(func, 1, 0x42, 0x00", "sdio_writeb(func, 0, 0xff"]
        positions = [init.index(w) for w in writes]
        self.assertEqual(positions, sorted(positions))
        self.assertEqual(init.count("if (error)"), 13)
        self.assertIn("0xff0000ff", init)
        self.assertIn("r->mac_stage = 7", init)
        self.assertNotIn("WIFI_HIMR", init)
        self.assertLess(C.index("error = wifi_command_mac_init(func, r)"),
                        C.index("wifi_command_snapshot(func, r, 0)"))
        self.assertIn("mac_stage=%u", D)

    def test_normal_command_diagnostic(self):
        for text in ("0x8c200010", "0x00110008 | ((u32)seq << 24)",
                     "packet[40] = 1", "r->opmode && !r->scanning && code == 19 && r->sent",
                     'sizeof("set opmode: 00000001\\n")', "if (r->opmode)"):
            self.assertIn(text, C)
        self.assertIn('sysfs_streq(buf, "opmode")', D)
        self.assertNotIn("r->pmc_after != 0x3b", C)
        self.assertNotIn("rf_after", C + D)

    def test_passive_survey_bounds(self):
        for text in ("0x8c200060", "0x00120058", "p[50] = 1",
                     "p[51] = 1; p[52] = 6; p[53] = 11", "p[83] = 3",
                     "r->scanning ? 750 : 100", "length < 116",
                     "get_unaligned_le32(bss + 12) > 32",
                     "get_unaligned_le32(bss + 112) > length - 116",
                     "r->survey_count != r->survey_events",
                     "r->survey_events >= 64"):
            self.assertIn(text, C)
        packet = C.split("static void wifi_survey_packet", 1)[1].split("/* Factory firmware reply", 1)[0]
        self.assertIn("memset(packet, 0, 512)", packet)
        self.assertNotIn("p[0] = 1", packet)
        self.assertIn('sysfs_streq(buf, "survey")', D)
        self.assertIn("survey_done=%d", D)

    def test_function_register_bus_contract(self):
        import re
        for name in ("power", "firmware", "irq", "events", "command"):
            source = (ROOT / ("h432b-wifi-" + name + ".h")).read_text()
            self.assertIsNone(re.search(r"(?<![a-z_])sdio_(?:readb|writeb)[(]", source))
            self.assertNotIn("wifi_wifi_", source)
        power = (ROOT / "h432b-wifi-power.h").read_text()
        self.assertIn("func->tmpbuf", power)
        self.assertIn("sdio_f0_readb", (ROOT / "h432b-wifi-firmware.h").read_text())

    def test_native_c2h_interrupt_mode(self):
        for token in ("opmode-irq", "survey-irq", "wifi_command_irq(func",
                      "irq_callbacks=%u", "irq_empty=%u"):
            self.assertIn(token, D)
        for token in ("sdio_claim_irq(func, handler)", "sdio_release_irq(func)",
                      "reinit_completion", "wait_for_completion_timeout",
                      "r->irq_status & (BIT(1) | (r->irq_owned ? BIT(0) : 0))", "r->irq_empty++",
                      "time_after_eq(jiffies, deadline)"):
            self.assertIn(token, C)
        callback = C.split("static void wifi_command_irq", 1)[1].split("static int wifi_command_arm", 1)[0]
        self.assertLess(callback.index("sdio_readw(func, WIFI_HISR"),
                        callback.index("sdio_writew(func, 0, WIFI_HIMR"))
        self.assertNotIn("mutex_lock", callback)
        wait = C.split("static int wifi_command_wait", 1)[1].split("static void wifi_command_snapshot", 1)[0]
        self.assertLess(wait.index("sdio_release_host(func)"),
                        wait.index("wait_for_completion_timeout"))
        self.assertLess(wait.index("wait_for_completion_timeout"),
                        wait.index("sdio_claim_host(func)"))
        self.assertNotIn("wifi_command_drain", wait)

    def test_sequence_wrap_and_repeated_survey(self):
        for token in ("r->stress ? 256", "r->survey_repeat ? 5",
                      "command < commands", "(command + first_command) & 0x7f",
                      "r->survey && command >= 2", "r->commands_done++",
                      "r->survey_runs++", "r->survey_total += r->survey_count"):
            self.assertIn(token, C)
        sequence = [(command + 1) & 0x7f for command in range(256)]
        self.assertEqual(sequence[:2], [1, 2])
        self.assertEqual(sequence[126:130], [127, 0, 1, 2])
        self.assertEqual(sequence[-1], 0)
        self.assertEqual(sequence.count(0), 2)
        self.assertNotIn("r->command_seq <= commands", C)
        reset = C.split("r->scanning = r->survey", 1)[1].split("tries =", 1)[0]
        for token in ("r->survey_events = 0", "r->survey_count = 0",
                      "r->survey_done = false"):
            self.assertIn(token, reset)
        self.assertIn('sysfs_streq(buf, "stress-irq")', D)
        self.assertIn('sysfs_streq(buf, "survey-repeat-irq")', D)

    def test_exclusive_stream_owner(self):
        self.assertIn("r->attempted || sample->event.attempted", D)
        self.assertIn("r->attempted || sample->command.attempted", D)
        self.assertIn("!sample->ack.attempted || sample->ack.error", D)

if __name__ == "__main__":
    unittest.main()

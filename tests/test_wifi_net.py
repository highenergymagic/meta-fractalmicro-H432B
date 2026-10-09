# SPDX-License-Identifier: MIT
"""Offline contracts for the opt-in cfg80211 interface; no hardware emulation."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1] / "recipes-kernel/linux/files"
NET = (ROOT / "h432b-wifi-net.h").read_text()
NET += (ROOT / "h432b-wifi-debug.h").read_text().split(
    "static ssize_t network_start_store")[1]
CMD = (ROOT / "h432b-wifi-command.h").read_text()
DRIVER = (ROOT / "h432b-wifi-transport.c").read_text()


class WifiNetwork(unittest.TestCase):
    def test_passive_scan_and_association_hooks(self):
        for token in ("IEEE80211_CHAN_NO_IR", "request->n_ssids",
                      "request->ie_len", "request->flags", "-EOPNOTSUPP",
                      "netif_carrier_off", "netif_tx_disable"):
            self.assertIn(token, NET)
        self.assertIn("request->flags & ~(NL80211_SCAN_FLAG_COLOCATED_6GHZ | NL80211_SCAN_FLAG_FLUSH)", NET)
        self.assertNotIn("netif_carrier_on", NET)
        self.assertIn(".connect = wifi_net_connect", NET)
        self.assertIn("dev->stats.tx_dropped++", NET)

    def test_bss_layout_and_length_checks(self):
        # u32 length + MAC/padding + SSID(length + 32) + privacy/RSSI/type
        configuration = 4 + 8 + 36 + 12
        self.assertEqual(configuration + 12, 72)  # DSConfig
        self.assertIn("get_unaligned_le32(bss + 72)", NET)
        for token in ("length < 128", "ie_len < 12",
                      "ie_len > length - 116", "ie_len - pos < 2",
                      "ies[pos + 1] > ie_len - pos - 2",
                      "cfg80211_put_bss", "CFG80211_SIGNAL_TYPE_NONE"):
            self.assertIn(token, NET)

    def test_queued_scan_is_completed_during_stop(self):
        stop = NET.split("static int wifi_net_stop", 1)[1].split(
            "static netdev_tx_t", 1)[0]
        self.assertLess(stop.index("cancelled, true"),
                        stop.index("cancel_work_sync"))
        self.assertLess(stop.index("cancel_work_sync"),
                        stop.index("wifi_net_finish_scan(net, true)"))
        self.assertIn("cfg80211_scan_done(request, &info)", NET)
        self.assertIn("device_remove_group", DRIVER)
        self.assertIn("wifi_net_unregister(sdio_get_drvdata(func))", DRIVER)

    def test_persistent_wire_state_is_not_reset_per_scan(self):
        for token in ("r->persistent && r->stream_started ? r->next_command : 1",
                      "r->next_command = (r->command_seq + 1) & 0x7f",
                      "if (!r->persistent || !r->stream_started)",
                      "if (!r->persistent)\n\t\t\tinit_completion",
                      "READ_ONCE(r->cancelled)", "return -ECANCELED"):
            self.assertIn(token, CMD)
        sequence = 1
        for _ in range(100):
            commands = [(sequence + n) & 127 for n in range(3)]
            sequence = (commands[-1] + 1) & 127
        self.assertEqual(sequence, 301 & 127)

    def test_firmware_and_stream_ownership_precede_registration(self):
        for token in ("owner->firmware.stage != 12", "owner->power.warm",
                      "owner->ack.error", "owner->command.attempted = true",
                      "is_valid_ether_addr(mac)", "eth_hw_addr_set(dev, mac)"):
            self.assertIn(token, NET)
        self.assertNotIn("eth_hw_addr_random", NET)
        self.assertNotIn("request_firmware", NET)


if __name__ == "__main__":
    unittest.main()

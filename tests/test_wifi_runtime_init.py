# SPDX-License-Identifier: MIT
"""Runtime initialization and diagnostic separation contracts."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1] / "recipes-kernel/linux"
FILES = ROOT / "files"
DRIVER = (FILES / "h432b-wifi-transport.c").read_text()
INIT = (FILES / "h432b-wifi-init.h").read_text()


class RuntimeInitialization(unittest.TestCase):
    def test_runtime_is_one_operation(self):
        attrs = DRIVER.split("static struct attribute *h432b_wifi_attrs[]")[1]
        public = attrs.split("#ifdef CONFIG_H432B_WIFI_DIAGNOSTICS")[0]
        self.assertIn("dev_attr_initialize", public)
        self.assertIn("dev_attr_network_result", public)
        self.assertNotIn("dev_attr_sample", public)
        self.assertNotIn("static ssize_t sample_store", DRIVER)
        self.assertIn('#include "h432b-wifi-debug.h"', DRIVER)

    def test_firmware_validated_before_writes(self):
        order = ["request_firmware_direct", "wifi_fw_validate",
                 "owner->startup_attempted = true", "wifi_runtime_power(func",
                 "wifi_fw_memory", "wifi_ack_test", "wifi_net_register"]
        positions = [INIT.index(token) for token in order]
        self.assertEqual(positions, sorted(positions))
        self.assertIn("imem, emem, true", INIT)
        self.assertIn("owner->firmware.stage != 12", INIT)
        self.assertIn("release_firmware(fw)", INIT)
        self.assertIn('dev_err(dev, "%s failed: %d', INIT)

    def test_idempotent_success_but_no_partial_hardware_replay(self):
        self.assertIn("if (owner->net)", INIT)
        self.assertIn("owner->net->faulted ? -EIO : 0", INIT)
        self.assertIn("if (owner->startup_attempted)", INIT)
        self.assertIn("error = owner->startup_error", INIT)
        self.assertLess(INIT.index("mutex_unlock"),
                        INIT.index("wifi_led_register"))

    def test_normal_logs_and_optional_snapshots(self):
        for name in ("h432b-wifi-transport.c", "h432b-wifi-net.h",
                     "h432b-wifi-assoc.h"):
            self.assertNotIn("dev_info(", (FILES / name).read_text())
        cmd = (FILES / "h432b-wifi-command.h").read_text()
        self.assertIn("!IS_ENABLED(CONFIG_H432B_WIFI_DIAGNOSTICS)", cmd)
        patch = (FILES / "0016-wifi-transport.patch").read_text()
        self.assertIn("+\tdefault n", patch)
        diagnostic = (ROOT / "linux-h432b-wifi-test_6.12.111.bb").read_text()
        self.assertIn("h432b-wifi-debug.config", diagnostic)

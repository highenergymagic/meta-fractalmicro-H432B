# SPDX-License-Identifier: MIT
"""Runtime initialization and diagnostic separation contracts."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1] / "recipes-kernel/linux"
FILES = ROOT / "files"
DRIVER = (FILES / "h432b-wifi-transport.c").read_text()
INIT = (FILES / "h432b-wifi-init.h").read_text()


class RuntimeInitialization(unittest.TestCase):
    def test_runtime_probe_and_diagnostic_only_manual_interfaces(self):
        self.assertIn("#ifdef CONFIG_H432B_WIFI_DIAGNOSTICS\nstatic struct attribute", DRIVER)
        self.assertIn("error = wifi_runtime_initialize(func)", DRIVER)
        self.assertIn("#ifdef CONFIG_H432B_WIFI_DIAGNOSTICS\nstatic ssize_t initialize_store", INIT)
        net = (FILES / "h432b-wifi-net.h").read_text()
        self.assertIn("#ifdef CONFIG_H432B_WIFI_DIAGNOSTICS\nstatic ssize_t network_result_show", net)
        self.assertIn("dev->ethtool_ops = &wifi_net_ethtool_ops", net)
        self.assertNotIn("static ssize_t sample_store", DRIVER)
        self.assertIn('#include "h432b-wifi-debug.h"', DRIVER)

    def test_runtime_module_and_builtin_diagnostic_configuration(self):
        config = (FILES / "h432b-wifi-runtime.config").read_text()
        for token in ("CONFIG_MODULES=y", "CONFIG_MODULE_UNLOAD=y", "CONFIG_RTL8712S=m"):
            self.assertIn(token, config)
        self.assertIn("CONFIG_RTL8712S=y", (FILES / "h432b-wifi-net.config").read_text())
        recipe = (ROOT / "linux-h432b-runtime_6.12.111.bb").read_text()
        self.assertIn('H432B_WIFI_DRIVER_MODE = "m"', recipe)
        self.assertIn("Wrong final RTL8712S mode", recipe)
        diagnostic = (ROOT / "linux-h432b-wifi-test_6.12.111.bb").read_text()
        self.assertIn('H432B_WIFI_DRIVER_MODE = "y"', diagnostic)

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

    def test_idempotent_success_and_full_reset_for_retry(self):
        self.assertIn("if (owner->net)", INIT)
        self.assertIn("owner->net->faulted ? -EIO : 0", INIT)
        self.assertIn("wifi_runtime_boot(func, owner, owner->startup_attempted)", INIT)
        self.assertIn("error = wifi_power_off(func)", INIT)
        self.assertIn("owner->command.irq_owned || func->irq_handler", INIT)
        self.assertIn("init_completion(&owner->command.irq_done)", INIT)
        self.assertIn("wifi_runtime_shutdown(func)", DRIVER)
        self.assertLess(INIT.index("mutex_unlock"),
                        INIT.index("wifi_led_register"))

    def test_normal_logs_and_optional_snapshots(self):
        for name in ("h432b-wifi-transport.c", "h432b-wifi-assoc.h"):
            self.assertNotIn("dev_info(", (FILES / name).read_text())
        net = (FILES / "h432b-wifi-net.h").read_text()
        self.assertEqual(net.count("dev_info("), 2)
        self.assertIn("RTL8712S SDIO station interface registered", net)
        self.assertIn("radio recovered; reassociation required", net)
        cmd = (FILES / "h432b-wifi-command.h").read_text()
        self.assertIn("!IS_ENABLED(CONFIG_H432B_WIFI_DIAGNOSTICS)", cmd)
        patch = (FILES / "0016-wifi-transport.patch").read_text()
        self.assertIn("+\tdefault n", patch)
        diagnostic = (ROOT / "linux-h432b-wifi-test_6.12.111.bb").read_text()
        self.assertIn("h432b-wifi-debug.config", diagnostic)

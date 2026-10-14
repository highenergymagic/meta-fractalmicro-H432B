# SPDX-License-Identifier: MIT
"""Administrative radio lifetime, separate from system sleep and recovery."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

FILES = Path(__file__).resolve().parents[1] / "recipes-kernel/linux/files"


class RadioLifecycle(unittest.TestCase):
    def test_mmc_owns_board_power_sequence(self):
        dt = (FILES / "s5pv210-hims-u2-wifi.dtsi").read_text()
        for token in ("regulator-fixed", "<&gpe0 3 GPIO_ACTIVE_HIGH>",
                      "mmc-pwrseq-simple", "<&gph1 0 GPIO_ACTIVE_HIGH>",
                      "off-on-delay-us = <200000>",
                      "post-power-on-delay-ms = <200>",
                      "vmmc-supply = <&wifi_supply>",
                      "mmc-pwrseq = <&wifi_pwrseq>",
                      "/delete-node/ wifi-enable-hog",
                      "/delete-node/ wifi-reset-hog"):
            self.assertIn(token, dt)
        self.assertNotIn("regulator-always-on", dt)
        for name in ("h432b-wifi-net.config", "h432b-wifi-runtime.config"):
            config = (FILES / name).read_text()
            self.assertIn("CONFIG_PWRSEQ_SIMPLE=y", config)
            self.assertIn("CONFIG_REGULATOR_FIXED_VOLTAGE=y", config)

    def test_transport_reset_precedes_firmware_restart(self):
        init = (FILES / "h432b-wifi-init.h").read_text()
        restart = init.split("static int wifi_runtime_restart")[1].split(
            "static int wifi_runtime_shutdown")[0]
        order = [restart.index(s) for s in (
            "command.irq_owned", "sdio_claim_host", "mmc_hw_reset",
            "sdio_release_host", "wifi_runtime_boot")]
        self.assertEqual(order, sorted(order))
        self.assertIn("return error < 0 ? error : -EAGAIN;", restart)

    def test_administrative_close_is_separate(self):
        net = (FILES / "h432b-wifi-net.h").read_text()
        driver = (FILES / "h432b-wifi-transport.c").read_text()
        self.assertIn(".ndo_stop = wifi_net_close", net)
        close = net.split("static int wifi_net_close")[1].split(
            "static void wifi_net_recovery_work")[0]
        order = [close.index(s) for s in (
            "wifi_net_stop(dev)", "mutex_lock", "wifi_runtime_shutdown",
            "mutex_unlock")]
        self.assertEqual(order, sorted(order))
        self.assertIn("net->last_error = error", close)
        self.assertIn("wifi_net_stop(net->dev)", driver)
        self.assertNotIn("wifi_net_close(net->dev)", driver)
        self.assertIn("net->needs_restart || net->owner->radio_off", net)

    def test_stop_cancellation_is_not_a_radio_fault(self):
        net = (FILES / "h432b-wifi-net.h").read_text()
        self.assertIn("error != -ECANCELED || !READ_ONCE(net->stopping)", net)
        self.assertIn("else if (mask_error)", net)

    def test_initial_down_and_cold_restart(self):
        driver = (FILES / "h432b-wifi-transport.c").read_text()
        init = (FILES / "h432b-wifi-init.h").read_text()
        self.assertIn("wifi_runtime_boot(net->func, net->owner, false)", init)
        self.assertIn("owner->radio_off = false", init)
        probe = driver.split("static int h432b_wifi_probe")[1].split(
            "static void h432b_wifi_remove")[0]
        order = [probe.index(s) for s in (
            "rtnl_lock()", "mutex_lock", "!netif_running",
            "wifi_runtime_shutdown", "mutex_unlock", "rtnl_unlock()")]
        self.assertEqual(order, sorted(order))

    def test_native_shutdown_failures_and_idempotence(self):
        cc = os.environ.get("WIFI_RX_NATIVE_CC")
        if not cc:
            self.skipTest("pinned-container native compiler not configured")
        src = (FILES / "h432b-wifi-init.h").read_text()
        fn = "static int wifi_runtime_shutdown" + src.split(
            "static int wifi_runtime_shutdown", 1)[1].split(
            "static int wifi_runtime_initialize", 1)[0]
        harness = r'''
#include <stdbool.h>
#include <stdint.h>
#include <assert.h>
#include <errno.h>
typedef uint8_t u8;
#define BIT(x) (1U << (x))
#define SDIO_CCCR_IOEx 2
struct h432b_wifi_sample { bool radio_off; };
struct sdio_func { unsigned int num; };
static struct h432b_wifi_sample owner;
static int held, operations, fail_at, enabled, powered;
static void *sdio_get_drvdata(struct sdio_func *f) { return &owner; }
static void sdio_claim_host(struct sdio_func *f) { assert(!held++); }
static void sdio_release_host(struct sdio_func *f) { assert(held-- == 1); }
static int operation(void) {
 assert(held); return ++operations == fail_at ? -EIO : 0;
}
static u8 sdio_f0_readb(struct sdio_func *f, int reg, int *err) {
 *err = operation(); return enabled ? BIT(f->num) : 0;
}
static int sdio_enable_func(struct sdio_func *f) {
 int err = operation(); if (!err) enabled = 1; return err;
}
static int sdio_disable_func(struct sdio_func *f) {
 int err = operation(); if (!err) enabled = 0; return err;
}
static int wifi_power_off(struct sdio_func *f) {
 int err = operation(); if (!err) powered = 0; return err;
}
'''
        main = r'''
int main(void) {
 struct sdio_func func = { .num = 1 };
 for (int initial = 0; initial <= 1; initial++) {
  int count = initial ? 2 : 4;
  for (int fail = 0; fail <= count; fail++) {
   owner.radio_off = false; held = operations = 0;
   enabled = initial; powered = 1; fail_at = fail;
   int err = wifi_runtime_shutdown(&func);
   assert(!held);
   assert(owner.radio_off == (err == 0));
   if (fail) assert(err == -EIO);
   else {
    assert(!err && !powered && enabled == initial);
    int saved = operations;
    assert(!wifi_runtime_shutdown(&func));
    assert(operations == saved);
   }
  }
 }
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp)
            (path / "test.c").write_text(harness + fn + main)
            subprocess.run([cc, "-std=c11", "-Wall", "-Wextra",
                            "-Wno-unused-parameter", "-Werror",
                            str(path / "test.c"), "-o", str(path / "test")], check=True)
            subprocess.run([str(path / "test")], check=True)

    def test_native_restart_reset_errors(self):
        cc = os.environ.get("WIFI_RX_NATIVE_CC")
        if not cc:
            self.skipTest("pinned-container native compiler not configured")
        src = (FILES / "h432b-wifi-init.h").read_text()
        fn = "static int wifi_runtime_restart" + src.split(
            "static int wifi_runtime_restart", 1)[1].split(
            "/* Called after packet work", 1)[0]
        harness = r'''
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
typedef uint8_t u8;
#define ETH_ALEN 6
struct mmc_card { int unused; };
struct sdio_func { struct mmc_card *card; void *irq_handler; };
struct owner { struct { bool irq_owned; } command; bool radio_off; };
struct net_device { u8 dev_addr[ETH_ALEN]; };
struct h432b_wifi_net {
 struct sdio_func *func; struct owner *owner; struct net_device *dev;
};
static int held, resets, boots, streams, reset_error, boot_error;
static void sdio_claim_host(struct sdio_func *f) { assert(!held++); }
static void sdio_release_host(struct sdio_func *f) { assert(held-- == 1); }
static int mmc_hw_reset(struct mmc_card *c) {
 assert(held); resets++; return reset_error;
}
static int wifi_runtime_boot(struct sdio_func *f, struct owner *o, bool reset) {
 assert(!held && resets == 1 && !reset); boots++; return boot_error;
}
static int wifi_net_read_mac(struct sdio_func *f, struct owner *o, u8 *mac) {
 memset(mac, 0, ETH_ALEN); return 0;
}
static bool ether_addr_equal(const u8 *a, const u8 *b) {
 return !memcmp(a, b, ETH_ALEN);
}
static void wifi_net_stream_init(struct h432b_wifi_net *n) { streams++; }
'''
        main = r'''
int main(void) {
 struct mmc_card card = {};
 struct sdio_func func = { .card = &card };
 struct owner owner = {};
 struct net_device dev = {};
 struct h432b_wifi_net net = { &func, &owner, &dev };
 owner.command.irq_owned = true;
 assert(wifi_runtime_restart(&net) == -EBUSY && !resets);
 owner.command.irq_owned = false; func.irq_handler = &net;
 assert(wifi_runtime_restart(&net) == -EBUSY && !resets);
 func.irq_handler = 0; reset_error = -EIO;
 assert(wifi_runtime_restart(&net) == -EIO && !boots && !held);
 reset_error = 1; resets = 0;
 assert(wifi_runtime_restart(&net) == -EAGAIN && !boots && !held);
 reset_error = 0; resets = 0; boot_error = -ETIMEDOUT;
 assert(wifi_runtime_restart(&net) == -ETIMEDOUT && !streams && !held);
 resets = boots = 0; boot_error = 0;
 assert(!wifi_runtime_restart(&net) && boots == 1 && streams == 1 && !held);
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp)
            (path / "test.c").write_text(harness + fn + main)
            subprocess.run([cc, "-std=c11", "-Wall", "-Wextra",
                            "-Wno-unused-parameter", "-Werror",
                            str(path / "test.c"), "-o", str(path / "test")], check=True)
            subprocess.run([str(path / "test")], check=True)

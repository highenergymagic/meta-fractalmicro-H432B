# SPDX-License-Identifier: MIT
"""LED integration contracts, not an optical hardware qualification."""
from pathlib import Path
import unittest
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
FILES = ROOT / "recipes-kernel/linux/files"


class WifiLeds(unittest.TestCase):
    def test_explicit_chip_pins_and_readback(self):
        source = (FILES / "h432b-wifi-led.h").read_text()
        for token in ('"rtl8712::led0"', '"rtl8712::led1"',
                      "#define WIFI_LED_CFG 0x2f2",
                      "brightness_set_blocking", "after != value",
                      "sdio_claim_host", "sdio_release_host",
                      "mutex_lock(&owner->lock)", "netif_running",
                      "LED_CORE_SUSPENDRESUME"):
            self.assertIn(token, source)
        self.assertNotIn("default_trigger", source)

    def test_lifetime(self):
        source = (FILES / "h432b-wifi-transport.c").read_text()
        remove = source.split("static void h432b_wifi_remove")[1]
        self.assertLess(remove.index("device_remove_group"),
                        remove.index("wifi_led_unregister"))
        self.assertLess(remove.index("wifi_led_unregister"),
                        remove.index("wifi_net_unregister"))
        net = (FILES / "h432b-wifi-init.h").read_text()
        start = net.split("static int wifi_runtime_initialize")[1].split(
            "#ifdef CONFIG_H432B_WIFI_DIAGNOSTICS")[0]
        self.assertLess(start.index("mutex_unlock"),
                        start.index("wifi_led_register"))

    def test_restore_precedes_irq_enable(self):
        source = (FILES / "h432b-wifi-net.h").read_text()
        start = source.split("static int wifi_net_start", 1)[1].split(
            "static int wifi_net_open", 1)[0]
        self.assertLess(start.index("sdio_set_block_size"),
                        start.index("wifi_led_restore"))
        self.assertLess(start.index("wifi_led_restore"), start.index("sdio_claim_irq"))
        self.assertIn("error = wifi_led_restore(net->owner);\n\tif (error)\n\t\tgoto release;", start)

    def test_standard_build_inputs(self):
        recipe = (ROOT / "recipes-kernel/linux/h432b-wifi.inc").read_text()
        self.assertIn("file://h432b-wifi-led.h", recipe)
        self.assertIn("${UNPACKDIR}/h432b-wifi-*.h", recipe)
        config = (FILES / "h432b-wifi-net.config").read_text()
        self.assertIn("CONFIG_NEW_LEDS=y", config)
        self.assertIn("CONFIG_LEDS_CLASS=y", config)

    def test_native_register_updates_and_failures(self):
        cc = os.environ.get("WIFI_RX_NATIVE_CC")
        if not cc:
            self.skipTest("pinned-container native compiler not configured")
        source = (FILES / "h432b-wifi-led.h").read_text()
        implementation = source.split("struct h432b_wifi_led {", 1)[1].split(
            "static void wifi_led_unregister", 1)[0]
        harness = r"""
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <errno.h>
#include <assert.h>
typedef uint8_t u8;
enum led_brightness { LED_OFF, LED_FULL };
struct led_classdev { unsigned int flags; };
#define LED_UNREGISTERING 1U
struct sdio_func { int unused; };
struct net_device { int running; };
struct h432b_wifi_net { struct net_device *dev; int stopping, faulted; };
struct h432b_wifi_led;
struct h432b_wifi_sample {
 int lock;
 struct h432b_wifi_net *net;
 struct h432b_wifi_led *leds;
 bool radio_off;
};
#define container_of(p,t,m) ((t *)((char *)(p) - offsetof(t,m)))
#define READ_ONCE(x) (x)
#define WIFI_LED_CFG 0x2f2
static int held, locked, ops, fail_at, mismatch;
static u8 reg;
static void mutex_lock(int *p) { assert(!locked++); }
static void mutex_unlock(int *p) { assert(locked-- == 1); }
static void sdio_claim_host(struct sdio_func *f) { assert(!held++); }
static void sdio_release_host(struct sdio_func *f) { assert(held-- == 1); }
static int netif_running(struct net_device *d) { return d->running; }
static u8 wifi_read(struct sdio_func *f, int w, int off, int *err) {
 assert(held && locked && w == 1 && off == WIFI_LED_CFG);
 if (++ops == fail_at) *err = -EIO;
 return reg ^ (mismatch && ops == 3);
}
static void wifi_write(struct sdio_func *f, int w, int off, u8 val, int *err) {
 assert(held && locked && w == 1 && off == WIFI_LED_CFG);
 if (++ops == fail_at) *err = -EIO; else reg = val;
}
"""
        harness += "struct h432b_wifi_led {" + implementation
        harness += r"""
int main(void) {
 struct net_device dev = { 1 };
 struct h432b_wifi_net net = { &dev, 0, 0 };
 struct h432b_wifi_sample owner = { .net = &net };
 struct sdio_func func = { 0 };
 struct h432b_wifi_led led = { .func = &func, .owner = &owner };
 for (unsigned int pin = 0; pin < 2; pin++)
  for (unsigned int before = 0; before < 256; before++)
   for (unsigned int on = 0; on < 2; on++) {
    unsigned int mask = pin ? 0x0f : 0xf0;
    unsigned int off = pin ? 0x80 : 0x08;
    reg = before; ops = 0; led.pin = pin;
    assert(wifi_led_set(&led.cdev, on) == 0);
    assert(reg == ((before & mask) | (on ? 0 : off)));
    assert(ops == 3 && !held && !locked);
   }
 for (fail_at = 1; fail_at <= 3; fail_at++) {
  ops = 0;
  assert(wifi_led_set(&led.cdev, LED_FULL) == -EIO);
  assert(!held && !locked && ops == fail_at);
 }
 fail_at = 0; mismatch = 1; ops = 0;
 assert(wifi_led_set(&led.cdev, LED_FULL) == -EIO);
 assert(!held && !locked);
 mismatch = 0;
 for (int mode = 0; mode < 5; mode++) {
  owner.net = mode == 0 ? NULL : &net;
  dev.running = mode != 1;
  net.stopping = mode == 2; net.faulted = mode == 3;
  owner.radio_off = mode == 4; ops = 0;
  assert(wifi_led_set(&led.cdev, LED_FULL) == 0);
  assert(led.configured && led.requested == LED_FULL);
  assert(wifi_led_set(&led.cdev, LED_OFF) == 0);
  assert(led.requested == LED_OFF);
  led.cdev.flags = LED_UNREGISTERING;
  assert(wifi_led_set(&led.cdev, LED_OFF) == 0);
  led.cdev.flags = 0;
  assert(!ops && !held && !locked);
 }
 owner.net = &net; dev.running = 1; net.stopping = net.faulted = 0;
 owner.radio_off = false;
 struct h432b_wifi_led leds[2] = {
  { .func = &func, .owner = &owner, .pin = 0 },
  { .func = &func, .owner = &owner, .pin = 1 },
 };
 /* Missing/unconfigured LEDs must leave firmware defaults alone. */
 mutex_lock(&owner.lock); sdio_claim_host(&func); ops = 0;
 assert(wifi_led_restore(&owner) == 0 && ops == 0);
 owner.leds = leds;
 assert(wifi_led_restore(&owner) == 0 && ops == 0);
 sdio_release_host(&func); mutex_unlock(&owner.lock);
 /* Off/on across a radio power cycle: the last request wins. */
 owner.radio_off = true;
 assert(wifi_led_set(&leds[0].cdev, LED_OFF) == 0);
 assert(wifi_led_set(&leds[0].cdev, LED_FULL) == 0);
 assert(wifi_led_set(&leds[1].cdev, LED_OFF) == 0);
 assert(ops == 0);
 owner.radio_off = false; reg = 0xff;
 mutex_lock(&owner.lock); sdio_claim_host(&func);
 assert(wifi_led_restore(&owner) == 0 && reg == 0x80 && ops == 6);
 /* Failed restore retains the request and returns the real bus error. */
 fail_at = 1; ops = 0;
 assert(wifi_led_restore(&owner) == -EIO);
 assert(leds[0].requested == LED_FULL);
 fail_at = 0;
 sdio_release_host(&func); mutex_unlock(&owner.lock);
 /* LED core's suspend OFF and resume ON may both arrive while stopped. */
 net.stopping = 1; ops = 0;
 assert(wifi_led_set(&leds[0].cdev, LED_OFF) == 0);
 assert(wifi_led_set(&leds[0].cdev, LED_FULL) == 0);
 assert(ops == 0);
 net.stopping = 0; reg = 0xff;
 mutex_lock(&owner.lock); sdio_claim_host(&func);
 assert(wifi_led_restore(&owner) == 0 && reg == 0x80);
 sdio_release_host(&func); mutex_unlock(&owner.lock);
 assert(!held && !locked);
 return 0;
}
"""
        with tempfile.TemporaryDirectory() as folder:
            c = Path(folder) / "led.c"
            executable = Path(folder) / "led"
            c.write_text(harness)
            subprocess.run([cc, "-std=c11", "-Wall", "-Werror",
                            str(c), "-o", str(executable)], check=True)
            subprocess.run([str(executable)], check=True)

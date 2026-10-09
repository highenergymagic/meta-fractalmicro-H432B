# SPDX-License-Identifier: MIT
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
FILES = ROOT / "recipes-kernel/linux/files"
BACKEND = ROOT / "recipes-support/brltty/files"

class Braille(unittest.TestCase):
    def test_runtime_inclusion(self):
        recipe = (ROOT / "recipes-kernel/linux/linux-h432b-runtime_6.12.111.bb").read_text()
        self.assertIn("require h432b-braille.inc", recipe)
        self.assertIn('s5pv210-hims-u2-braille.dtsi',
                      (FILES / "s5pv210-hims-u2-runtime.dts").read_text())

    def test_gpio_ownership_and_power_boundary(self):
        source = (FILES / "h432b-braille.c").read_text()
        self.assertNotIn("writel", source)
        self.assertIn('"enable", GPIOD_ASIS', source)
        self.assertIn("gpiod_set_value_cansleep(h->enable, 0)", source)
        self.assertIn("gpiod_set_value_cansleep(h->enable, 1)", source)
        dt = (FILES / "s5pv210-hims-u2-braille.dtsi").read_text()
        for bank, pin in (("gpj0", 3), ("gpj1", 5), ("gpj1", 4), ("gpj4", 1)):
            self.assertIn(f"<&{bank} {pin} GPIO_ACTIVE_HIGH>", dt)

    def test_inherited_direction_readback(self):
        include = (ROOT / "recipes-kernel/linux/h432b-braille.inc").read_text()
        self.assertIn("0020-samsung-gpio-direction-readback.patch", include)
        patch = (FILES / "0020-samsung-gpio-direction-readback.patch").read_text()
        for token in ("get_direction = samsung_gpio_get_direction",
                      "clk_enable", "clk_disable", "raw_spin_lock_irqsave",
                      "PINCFG_TYPE_FUNC", "shift >= 32", "return 1", "EINT mux"):
            self.assertIn(token, patch)
        self.assertNotIn("writel", patch)

    def test_shutdown_lowers_dots_and_removes_supply(self):
        source = (FILES / "h432b-braille.c").read_text()
        self.assertIn(".shutdown = display_shutdown,", source)
        body = source[source.index("static void display_shutdown"):]
        body = body[:body.index("\n}\n")]
        self.assertLess(body.index("shift_frame(h, blank)"), body.index("msleep(100)"))
        self.assertLess(body.index("msleep(100)"), body.index("gpiod_set_value_cansleep(h->enable, 0)"))
        self.assertIn("h->dead = true;", body)

    def test_frame_abi(self):
        source = (FILES / "h432b-braille.c").read_text()
        for token in ("count != sizeof(cells)", "copy_from_user", "-EBUSY",
                      "-EHOSTDOWN", "kref_get", "h->dead = true", ".mode = 0600"):
            self.assertIn(token, source)
        self.assertIn("cell = 31; cell >= 0; cell--", source)
        self.assertIn("bit = 7; bit >= 0; bit--", source)

    def test_backend_recovery_and_bindings(self):
        source = (BACKEND / "braille.c").read_text()
        for token in ("EVIOCGRAB", "EVIOCGKEY", "SYN_DROPPED", "SYN_REPORT",
                      "O_NONBLOCK", "BRL_CMD_RESTARTBRL", "budget < 256"):
            self.assertIn(token, source)
        self.assertIn("../hm/scroll.kti", (BACKEND / "all.ktb").read_text())

    @unittest.skipUnless(os.environ.get("WIFI_RX_NATIVE_CC"), "pinned native C compiler required")
    def test_actual_wire_and_key_helpers(self):
        code = r'''
#include <linux/input.h>
#include <assert.h>
#include "h432b-braille-wire.h"
#include "h432b-keys.h"
int main(void) {
  const unsigned char expected[] = {8,4,2,128,64,32,1,16};
  unsigned char seen[256] = {0};
  for (unsigned int i=0; i<8; i++)
    assert(h432_braille_wire(1U<<i) == (expected[i]^0x55));
  for (unsigned int i=0; i<256; i++)
    assert(!seen[h432_braille_wire(i)]++);
  assert(h432_braille_wire(0) == 0x55);
  assert(h432_key_number(0, KEY_BACKSPACE) == 6);
  assert(h432_key_number(0, KEY_ENTER) == 7);
  for (unsigned int i=0; i<6; i++)
    assert(h432_key_number(0, KEY_BRL_DOT1+i) == (int)i);
  for (unsigned int i=0; i<32; i++)
    assert(h432_key_number(1, BTN_TRIGGER_HAPPY1+i) == (int)i);
  assert(h432_key_number(0, KEY_POWER) == -1);
  assert(h432_key_number(1, KEY_ENTER) == -1);
  return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / "test.c"
            binary = Path(tmp) / "test"
            src.write_text(code)
            subprocess.run([os.environ["WIFI_RX_NATIVE_CC"], "-Wall", "-Wextra",
                            "-Werror", "-I"+str(FILES), "-I"+str(BACKEND),
                            str(src), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

# SPDX-License-Identifier: MIT
"""Factory-derived Bluetooth wiring contracts; not hardware emulation."""
from pathlib import Path
import unittest
import os
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[1]
FILES = ROOT / "recipes-kernel/linux/files"
class Bluetooth(unittest.TestCase):
    def test_uart_and_power_are_scoped(self):
        text = (FILES / "s5pv210-hims-u2-bluetooth.dtsi").read_text()
        self.assertIn("&uart0", text)
        self.assertIn("<&uart0_data &uart0_fctl>", text)
        self.assertIn("<&gpe0 2 GPIO_ACTIVE_HIGH>", text)
        self.assertIn("startup-delay-us = <300000>", text)
        self.assertNotIn("&uart1", text)
        self.assertNotIn("system5v", text.lower())
        self.assertNotIn("wakeup-source", text)
    def test_bcsp_not_guessed_vendor_driver(self):
        config = (FILES / "h432b-bluetooth.config").read_text()
        for option in ("BT", "BT_BREDR", "BT_HCIUART", "BT_HCIUART_BCSP"):
            self.assertIn("CONFIG_" + option + "=y", config)
        self.assertNotIn("CONFIG_BT_HCIUART_BCM=y", config)
    def test_runtime_integration(self):
        self.assertIn('s5pv210-hims-u2-bluetooth.dtsi',
                      (FILES / "s5pv210-hims-u2-runtime.dts").read_text())
        self.assertIn("require h432b-bluetooth.inc",
                      (ROOT / "recipes-kernel/linux/linux-h432b-runtime_6.12.111.bb").read_text())

    @unittest.skipUnless(os.environ.get("BT_NATIVE_CC"), "requires pinned native compiler")
    def test_actual_termios2_adapter(self):
        source = ROOT / "recipes-connectivity/bluez5/files/hciattach_h432b.c"
        harness = r'''
#define ioctl mocked_ioctl
#include "hciattach_h432b.c"
#undef ioctl
#include <stdarg.h>
#include <assert.h>
static int calls, fail_at;
static struct termios2 state;
int mocked_ioctl(int fd, unsigned long request, ...) {
    (void)fd;
    va_list ap; va_start(ap, request);
    struct termios2 *p = va_arg(ap, struct termios2 *);
    va_end(ap);
    if (++calls == fail_at) { errno = EIO; return -1; }
    if (request == TCGETS2) *p = state;
    else if (request == TCSETS2) state = *p;
    else assert(0);
    return 0;
}
int main(void) {
    assert(h432b_set_custom_baud(0,115200)==-EINVAL && calls==0);
    state.c_cflag=PARENB|CS8|CLOCAL;
    assert(h432b_set_custom_baud(0,1382400)==0);
    assert(calls==3 && state.c_ispeed==1382400 && state.c_ospeed==1382400);
    assert((state.c_cflag & (PARENB|CS8|CLOCAL))==(PARENB|CS8|CLOCAL));
    assert((state.c_cflag&CBAUD)==BOTHER);
    for (int n=1;n<=3;n++) {
        calls=0; fail_at=n;
        assert(h432b_set_custom_baud(0,1382400)==-EIO);
    }
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            test = Path(directory) / "test.c"
            binary = Path(directory) / "test"
            test.write_text(harness)
            subprocess.run([os.environ["BT_NATIVE_CC"], "-std=c11", "-Wall",
                            "-Wextra", "-Werror", "-I", str(source.parent),
                            str(test), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

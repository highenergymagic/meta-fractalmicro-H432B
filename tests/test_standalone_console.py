# SPDX-License-Identifier: MIT
"""Normal NAND boot must not depend on a USB host reading its console."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]


class StandaloneConsole(unittest.TestCase):
    def test_runtime_overrides_diagnostic_console(self):
        source = (ROOT / "recipes-kernel/linux/files/s5pv210-hims-u2-runtime.dts").read_text()
        chosen = source.split("&{/chosen}", 1)[1].split("};", 1)[0]
        args = re.search(r'bootargs = "([^"]+)";', chosen).group(1).split()
        self.assertIn("console=tty0", args)
        self.assertIn("systemd.show_status=no", args)
        self.assertIn("loglevel=4", args)
        self.assertNotIn("ignore_loglevel", args)
        self.assertFalse(any(a.startswith("console=ttyGS") for a in args))
        self.assertIn("g_serial.iProduct=OpenH432-RAM", args)

    def test_base_diagnostic_console_unchanged(self):
        source = (ROOT / "recipes-kernel/linux/files/s5pv210-hims-u2.dts").read_text()
        self.assertIn("console=ttyGS0,115200", source)

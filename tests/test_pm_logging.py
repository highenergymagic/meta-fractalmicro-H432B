# SPDX-License-Identifier: MIT
"""Successful low-level PM transitions must not emit unconditional traces."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
FILES = ROOT / "recipes-kernel/linux/files"


class PmLogging(unittest.TestCase):
    def test_success_trace_is_dynamic_debug(self):
        name = "0033-irqchip-pinctrl-pm-debug.patch"
        patch = (FILES / name).read_text()
        added = [line[1:] for line in patch.splitlines()
                 if line.startswith("+") and not line.startswith("+++")]
        self.assertEqual(sum("pr_debug(" in line for line in added), 3)
        self.assertFalse(any("pr_info(" in line or "printk(" in line
                             for line in added))
        self.assertIn(name, (FILES.parent / "h432b-suspend.inc").read_text())


if __name__ == "__main__":
    unittest.main()

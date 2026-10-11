# SPDX-License-Identifier: MIT
"""An unresolved firmware dependency remains an error with useful identity."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
KERNEL = ROOT / "recipes-kernel/linux"


class FirmwareLinkDiagnostics(unittest.TestCase):
    def test_error_retains_consumer_identity_and_severity(self):
        name = "0036-fw-devlink-consumer.patch"
        patch = (KERNEL / "files" / name).read_text()
        added = "\n".join(line[1:] for line in patch.splitlines()
                          if line.startswith("+") and not line.startswith("+++"))
        self.assertIn("dev_err(con,", added)
        self.assertIn("%pfwf", added)
        self.assertIn("link->consumer", added)
        self.assertNotIn("dev_dbg", added)
        self.assertNotIn("fw_devlink=", added)
        self.assertIn(name, (KERNEL / "h432b-suspend.inc").read_text())


if __name__ == "__main__":
    unittest.main()

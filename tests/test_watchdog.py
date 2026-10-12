# SPDX-License-Identifier: MIT
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


class WatchdogConfig(unittest.TestCase):
    def test_runtime_uses_upstream_hardware_driver(self):
        config = (ROOT / 'recipes-kernel/linux/files/h432b-runtime.config').read_text()
        recipe = (ROOT / 'recipes-kernel/linux/linux-h432b-runtime_6.12.111.bb').read_text()
        for option in ('WATCHDOG', 'WATCHDOG_CORE', 'WATCHDOG_SYSFS', 'S3C2410_WATCHDOG'):
            self.assertIn(f'CONFIG_{option}=y\n', config)
            self.assertIn(option, recipe)
        self.assertIn('# CONFIG_WATCHDOG_NOWAYOUT is not set', config)


if __name__ == '__main__':
    unittest.main()

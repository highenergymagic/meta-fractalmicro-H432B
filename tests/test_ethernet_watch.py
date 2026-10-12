# SPDX-License-Identifier: MIT
"""Lockup instrumentation is an explicit target, not production boot policy."""
from pathlib import Path
import unittest

LINUX = Path(__file__).resolve().parents[1] / 'recipes-kernel/linux'


class EthernetWatch(unittest.TestCase):
    def test_diagnostic_inherits_runtime_without_becoming_default(self):
        recipe = (LINUX / 'linux-h432b-ethernet-watch_6.12.111.bb').read_text()
        self.assertIn('require linux-h432b-runtime_6.12.111.bb', recipe)
        self.assertIn('H432B_KERNEL_PROVIDER_REMOVE = "virtual/kernel"', recipe)
        self.assertIn('KERNEL_DEPLOYSUBDIR = "kernel-ethernet-watch"', recipe)
        runtime = (LINUX / 'linux-h432b-runtime_6.12.111.bb').read_text()
        self.assertNotIn('h432b-ethernet-watch.config', runtime)

    def test_detectors_report_without_forcing_a_reboot(self):
        config = (LINUX / 'files/h432b-ethernet-watch.config').read_text().splitlines()
        for name in ('SOFTLOCKUP_DETECTOR', 'DETECT_HUNG_TASK', 'MAGIC_SYSRQ',
                     'FUNCTION_TRACER', 'FUNCTION_GRAPH_TRACER'):
            self.assertIn(f'CONFIG_{name}=y', config)
        for name in ('BOOTPARAM_SOFTLOCKUP_PANIC', 'BOOTPARAM_HUNG_TASK_PANIC'):
            self.assertIn(f'# CONFIG_{name} is not set', config)


if __name__ == '__main__':
    unittest.main()

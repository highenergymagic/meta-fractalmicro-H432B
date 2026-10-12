# SPDX-License-Identifier: MIT
"""All NAND-handoff kernel profiles inherit the same integrity requirement."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1] / 'recipes-kernel/linux'


class RootIntegrity(unittest.TestCase):
    def test_shared_platform_enables_and_checks_integrity(self):
        recipe = (ROOT / 'linux-h432b-platform.inc').read_text()
        fragment = (ROOT / 'files/h432b-root-integrity.config').read_text()
        self.assertIn('file://h432b-root-integrity.config', recipe)
        self.assertIn('${UNPACKDIR}/h432b-root-integrity.config', recipe)
        for option in ('BLK_DEV_DM', 'DM_VERITY', 'CRYPTO_SHA256_ARM'):
            self.assertIn('CONFIG_' + option + '=y', fragment)
            self.assertIn(option, recipe)

    def test_handoff_profiles_inherit_platform(self):
        def inherits(name, visited=None):
            visited = set() if visited is None else visited
            self.assertNotIn(name, visited)
            visited.add(name)
            if name == 'linux-h432b-platform.inc':
                return True
            includes = [line.split()[1] for line in (ROOT / name).read_text().splitlines()
                        if line.startswith('require linux-h432b-')]
            return any(inherits(parent, visited.copy()) for parent in includes)
        for profile in ('runtime', 'battery-test', 'wifi-test', 'input-test',
                        'gps-test', 'sd-rw-test', 'external-sd-test',
                        'power-test', 'resume-test'):
            with self.subTest(profile=profile):
                self.assertTrue(inherits('linux-h432b-' + profile + '_6.12.111.bb'))

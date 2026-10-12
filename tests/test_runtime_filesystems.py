# SPDX-License-Identifier: MIT
"""Standard runtime filesystem support must not depend on test profiles."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
LINUX = ROOT / 'recipes-kernel/linux'


class RuntimeFilesystems(unittest.TestCase):
    def test_removable_filesystems_are_built_in_and_checked(self):
        config = (LINUX / 'files/h432b-runtime.config').read_text()
        recipe = (LINUX / 'linux-h432b-runtime_6.12.111.bb').read_text()
        for option in ('FAT_FS', 'VFAT_FS', 'EXFAT_FS', 'NLS_CODEPAGE_437',
                       'NLS_ISO8859_1', 'NLS_UTF8', 'EXT4_FS',
                       'EXT4_FS_POSIX_ACL', 'EXT4_FS_SECURITY'):
            with self.subTest(option=option):
                self.assertIn(f'CONFIG_{option}=y', config.splitlines())
                self.assertIn(option, recipe)
        self.assertIn('Missing runtime filesystem option', recipe)
        self.assertIn('CONFIG_EXFAT_DEFAULT_IOCHARSET="utf8"', config.splitlines())


if __name__ == '__main__':
    unittest.main()

# SPDX-License-Identifier: MIT
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
FILES = ROOT / 'recipes-bsp/u-boot/files'


class LoaderProgressTests(unittest.TestCase):
    def test_percentage_cells(self):
        cc = os.environ.get('POWER_NATIVE_CC') or shutil.which('cc')
        self.assertTrue(cc)
        source = r'''
#include <stdio.h>
#define U2_BRL_READ(addr) 0U
#define U2_BRL_WRITE(addr, value) ((void)(value))
#include "u2_braille.h"
int main(void)
{
    unsigned char dots[32];
    unsigned int percent;
    for (percent = 0; percent < 100; percent++) {
        if (u2_braille_progress_decode("loading kernel", percent, dots))
            return 1;
        if (fwrite(dots, 1, 32, stdout) != 32)
            return 2;
    }
    return !u2_braille_progress_decode("x", 100, dots) ||
           !u2_braille_progress_decode("abcdefghijklmnopqrstuvwxyzabc", 1, dots);
}
'''
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            (directory / 'test.c').write_text(source)
            subprocess.run([cc, '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                            '-I', str(FILES / 'modern'), str(directory / 'test.c'),
                            '-o', str(directory / 'test')], check=True)
            frames = subprocess.check_output([str(directory / 'test')])
        self.assertEqual(len(frames), 3200)
        digits = (26, 1, 3, 9, 25, 17, 11, 27, 19, 10)
        for percent in range(100):
            self.assertEqual(frames[percent * 32 + 29:percent * 32 + 32],
                             bytes((0x3c, digits[percent // 10], digits[percent % 10])))

    def test_load_updates_are_bounded(self):
        patch = (FILES / '0023-h432b-load-progress.patch').read_text()
        self.assertIn('percent = offset * 95 / size', patch)
        self.assertIn('percent >= previous + 5', patch)
        self.assertIn('u2_braille_progress(label, 99)', patch)


if __name__ == '__main__':
    unittest.main()

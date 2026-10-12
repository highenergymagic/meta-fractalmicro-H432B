# SPDX-License-Identifier: MIT
"""Exercise the actual menu state machine without opening hardware."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1] / 'recipes-bsp/u-boot'


def added_file(patch, name):
    section = patch.split('diff --git a/' + name + ' b/' + name + '\n', 1)[1]
    section = section.split('\ndiff --git ', 1)[0]
    return '\n'.join(line[1:] for line in section.splitlines()
                     if line.startswith('+') and not line.startswith('+++')) + '\n'


class RecoveryMenuTests(unittest.TestCase):
    def test_state_machine(self):
        compiler = shutil.which('cc')
        self.assertIsNotNone(compiler, 'native compiler required')
        patch = (ROOT / 'files/0019-h432b-recovery-menu.patch').read_text()
        header = added_file(patch, 'board/fractalmicro/h432b/recovery-menu.h')
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)
            (path / 'recovery-menu.h').write_text(header)
            (path / 'test.c').write_text(r'''
#include <assert.h>
#include <string.h>
#include "recovery-menu.h"
int main(void) {
    struct h432b_menu m = {0};
    unsigned i;
    m.intro = 1;
    assert(!strcmp(h432b_menu_label(&m), "boot loader menu"));
    assert(h432b_menu_step(&m, MENU_NONE) == MENU_STAY && m.intro);
    assert(h432b_menu_step(&m, MENU_SELECT) == MENU_STAY);
    assert(!m.intro && m.item == 0);
    m.intro = 1;
    h432b_menu_step(&m, MENU_PREVIOUS);
    assert(!m.intro && m.item == 4);
    m.intro = 1;
    h432b_menu_step(&m, MENU_NEXT);
    assert(!m.intro && m.item == 0);
    for (i = 0; i < 5; i++) assert(strlen(h432b_menu_labels[i]) <= 32);
    assert(h432b_menu_step(&m, MENU_NONE) == MENU_STAY && m.item == 0);
    assert(h432b_menu_step(&m, MENU_SELECT) == MENU_REBOOT);
    h432b_menu_step(&m, MENU_NEXT);
    assert(h432b_menu_step(&m, MENU_SELECT) == MENU_FASTBOOT);
    h432b_menu_step(&m, MENU_NEXT);
    assert(h432b_menu_step(&m, MENU_SELECT) == MENU_STAY && m.unavailable);
    h432b_menu_step(&m, MENU_NEXT);
    assert(m.unavailable && m.item == 2);
    h432b_menu_step(&m, MENU_SELECT);
    assert(!m.unavailable && m.item == 2);
    for (i = 3; i < 5; i++) {
        h432b_menu_step(&m, MENU_NEXT);
        assert(m.item == i);
        h432b_menu_step(&m, MENU_SELECT);
        assert(m.confirmation && !m.confirm_yes);
        assert(!strcmp(h432b_menu_label(&m), "cancel"));
        h432b_menu_step(&m, MENU_SELECT);
        assert(!m.confirmation && !m.unavailable);
        h432b_menu_step(&m, MENU_SELECT);
        h432b_menu_step(&m, MENU_NEXT);
        assert(m.confirm_yes);
        assert(h432b_menu_step(&m, MENU_SELECT) == MENU_STAY);
        assert(m.unavailable && !m.confirmation && !m.confirm_yes);
        h432b_menu_step(&m, MENU_SELECT);
    }
    h432b_menu_step(&m, MENU_NEXT);
    assert(m.item == 0);
    h432b_menu_step(&m, MENU_PREVIOUS);
    assert(m.item == 4);
    return 0;
}
''')
            subprocess.run([compiler, '-std=c99', '-Wall', '-Wextra', '-Werror',
                            str(path / 'test.c'), '-o', str(path / 'test')], check=True)
            subprocess.run([str(path / 'test')], check=True)

    def test_no_destructive_backend_and_row_is_released(self):
        patch = (ROOT / 'files/0019-h432b-recovery-menu.patch').read_text()
        source = added_file(patch, 'board/fractalmicro/h432b/recovery-menu.c')
        self.assertIn('setbits_le32(ROW + 4, BIT(2))', source)
        self.assertIn('armed = 0', source)
        self.assertIn('readl(MEDIA + 4) & BIT(4)', source)
        self.assertNotIn('readl(MEDIA + 4) & BIT(3)', source)
        self.assertIn('get_timer(started) < 2000', source)
        self.assertIn('clrbits_le32(MOTOR + 4, BIT(4))', source)
        self.assertIn('clrbits_le32(BEEPER + 4, BIT(0))', source)
        for forbidden in ('blk_dwrite', 'blk_derase', 'nand_erase', 'ubi_remove', 'mkfs'):
            self.assertNotIn(forbidden, source)


if __name__ == '__main__':
    unittest.main()

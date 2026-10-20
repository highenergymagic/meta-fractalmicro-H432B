# SPDX-License-Identifier: MIT
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_recovery_menu import added_file

FILES = Path(__file__).resolve().parents[1] / 'recipes-bsp/u-boot/files'


class StandaloneRecoveryMenuTests(unittest.TestCase):
    def test_final_menu_state_machine(self):
        original = (FILES / '0019-h432b-recovery-menu.patch').read_text()
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            name = 'board/fractalmicro/h432b/recovery-menu.h'
            header = root / name
            header.parent.mkdir(parents=True)
            header.write_text(added_file(original, name))
            subprocess.run(['git', 'apply', '--include=' + name,
                            str(FILES / '0022-h432b-standalone-recovery.patch')], cwd=root, check=True)
            source = root / 'test.c'
            source.write_text(r'''
#include <assert.h>
#include "board/fractalmicro/h432b/recovery-menu.h"
int main(void) {
    struct h432b_menu m = { .item = 2 };
    unsigned int i;
    assert(h432b_menu_step(&m, MENU_SELECT) == MENU_RECOVERY);
    for (i = 3; i <= 4; i++) {
        m.item = i;
        assert(h432b_menu_step(&m, MENU_SELECT) == MENU_STAY);
        assert(m.confirmation && !m.confirm_yes);
        assert(h432b_menu_step(&m, MENU_SELECT) == MENU_STAY);
        assert(!m.confirmation && !m.confirm_yes);
        h432b_menu_step(&m, MENU_SELECT);
        h432b_menu_step(&m, MENU_NEXT);
        assert(h432b_menu_step(&m, MENU_SELECT) ==
               (i == 3 ? MENU_FACTORY_RESET : MENU_WIPE_CACHE));
        assert(!m.confirmation && !m.confirm_yes);
    }
    return 0;
}
''')
            subprocess.run(['cc', '-Wall', '-Wextra', '-Werror', str(source),
                            '-o', str(root / 'test')], check=True)
            subprocess.run([str(root / 'test')], check=True)

    def test_recovery_does_not_consume_slot_attempt(self):
        patch = (FILES / '0022-h432b-standalone-recovery.patch').read_text()
        self.assertIn('Recovery neither consumes nor acknowledges an A/B boot attempt', patch)
        self.assertIn('recovery_image(buffer, size)', patch)
        self.assertIn('vol->reserved_pebs != 264', patch)
        self.assertIn('volume_digest(vol, actual)', patch)

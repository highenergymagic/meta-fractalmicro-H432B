# SPDX-License-Identifier: MIT
"""Compile the deployed NAND guard and validate both controller patch bodies."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
FILES = ROOT / "recipes-kernel/linux/files"


def added_file(patch_name, target):
    patch = (FILES / patch_name).read_text()
    section = patch.split("+++ b/" + target + "\n", 1)[1]
    section = section.split("diff --git", 1)[0]
    section = section.split("--- /dev/null", 1)[0]
    return "\n".join(line[1:] for line in section.splitlines()
                     if line.startswith("+")) + "\n"


class NandController(unittest.TestCase):
    def test_word_read_patch_applies_without_fuzz(self):
        target = "drivers/mtd/nand/raw/hims-u2-nand.c"
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / target
            source.parent.mkdir(parents=True)
            source.write_text(added_file("0008-nand-bch-window.patch", target))
            subprocess.run(["patch", "--silent", "--fuzz=0", "-p1", "-i",
                            str(FILES / "0015-nand-word-reads.patch")],
                           cwd=directory, check=True)
            text = source.read_text()
            self.assertIn("ioread32_rep", text)
            self.assertIn("NAND_CMD_PAGEPROG", text)
            self.assertIn("NAND_BBT_USE_FLASH", text)
            self.assertNotIn("dev_info(", text)

    @unittest.skipUnless(os.environ.get("WIFI_RX_NATIVE_CC"),
                         "native C compiler not configured")
    def test_real_guard_covers_full_linux_region(self):
        header = added_file("0008-nand-bch-window.patch",
                            "drivers/mtd/nand/raw/hims-u2-nand-guard.h")
        harness = r'''
#include <assert.h>
#include <limits.h>
#include "hims-u2-nand-guard.h"

static struct u2_nand_guard fresh(void)
{
    struct u2_nand_guard s = { .first = U2_BOOT_END, .end = U2_BBT_START };
    return s;
}

int main(void)
{
    unsigned int row;
    assert(u2_guard_window(U2_BOOT_END, U2_BBT_START - U2_BOOT_END));
    assert(!u2_guard_window(U2_BOOT_END, UINT_MAX));
    for (row = 0; row < U2_NAND_BYTES / U2_PAGE_BYTES; row++) {
        struct u2_nand_guard s = fresh();
        unsigned char address[5] = { 0, 0, row, row >> 8, row >> 16 };
        int allowed = row >= U2_BOOT_END / U2_PAGE_BYTES &&
                      row < U2_BBT_START / U2_PAGE_BYTES;
        assert(!u2_guard_command(&s, 0x80));
        assert((u2_guard_address(&s, address, 5) == 0) == allowed);
        if (!allowed)
            continue;
        assert(u2_guard_command(&s, 0x10)); /* Empty program is invalid. */
        assert(!u2_guard_output(&s, U2_PAGE_BYTES));
        assert(!u2_guard_command(&s, 0x70)); /* Split operation status. */
        assert(!u2_guard_output(&s, U2_OOB_BYTES));
        assert(u2_guard_output(&s, 1));
        assert(u2_guard_output(&s, UINT_MAX));
        assert(!u2_guard_command(&s, 0x10));
        assert(u2_guard_output(&s, 1)); /* Completed operation is closed. */
    }
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            (work / "hims-u2-nand-guard.h").write_text(header)
            (work / "guard-test.c").write_text(harness)
            binary = work / "guard-test"
            subprocess.run([os.environ["WIFI_RX_NATIVE_CC"], "-std=c11",
                            "-Wall", "-Wextra", "-Werror",
                            str(work / "guard-test.c"), "-o", str(binary)],
                           check=True)
            subprocess.run([str(binary)], check=True)

    def test_raw_inspection_stays_explicit_and_cannot_write(self):
        source = added_file("0002-nand-reader.patch",
                            "drivers/mtd/nand/raw/hims-u2-nand-ro.c")
        self.assertIn("mtd->flags &= ~MTD_WRITEABLE", source)
        self.assertNotIn("case NAND_OP_DATA_OUT_INSTR:", source)
        self.assertIn("NAND_ECC_ENGINE_TYPE_NONE", source)
        self.assertNotIn("dev_info(", source)


if __name__ == "__main__":
    unittest.main()

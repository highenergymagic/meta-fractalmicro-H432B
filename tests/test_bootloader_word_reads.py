# SPDX-License-Identifier: MIT
from pathlib import Path
import unittest
ROOT = Path(__file__).resolve().parents[1] / "recipes-bsp/u-boot"

class BootloaderWordReads(unittest.TestCase):
    def test_callback_precedes_scan(self):
        text = (ROOT / "files/nand/u2nand.c").read_text()
        self.assertLess(text.index("n->read_buf=read_buf;"),
                        text.index("ret=nand_scan_ident("))
        self.assertNotIn("n->read_byte=", text)
        self.assertIn("n->write_buf=no_write_buf;", text)

    def test_pinned_build_runs_exhaustive_fifo_test(self):
        text = (ROOT / "u-boot-h432b-nand_2012.10.bb").read_text()
        self.assertIn("${BUILD_CC}", text)
        self.assertIn("test-nand-read-fifo.c", text)
        self.assertIn("${B}/test-nand-read-fifo", text)
        self.assertIn("${UNPACKDIR}/nand/nand-read-fifo.h", text)
        test = (ROOT / "files/nand/test-nand-read-fifo.c").read_text()
        self.assertIn("len <= 4096", test)
        self.assertIn("offset < 8", test)

if __name__ == "__main__":
    unittest.main()

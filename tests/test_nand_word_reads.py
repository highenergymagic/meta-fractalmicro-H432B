# SPDX-License-Identifier: MIT
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
PATCH = ROOT / "recipes-kernel/linux/files/0015-nand-word-reads.patch"

class NandWordReads(unittest.TestCase):
    def test_patch_is_in_common_kernel_series(self):
        recipe = (ROOT / "recipes-kernel/linux/linux-h432b-base.inc").read_text()
        self.assertLess(recipe.index("0008-nand-bch-window.patch"),
                        recipe.index(PATCH.name))

    def test_byte_fallback_covers_forced_and_unaligned_operations(self):
        added = "\n".join(line[1:] for line in PATCH.read_text().splitlines()
                          if line.startswith("+") and not line.startswith("+++"))
        self.assertIn("!in->ctx.data.force_8bit", added)
        self.assertIn("IS_ALIGNED((unsigned long)in->ctx.data.buf.in, 4)", added)
        self.assertIn("IS_ALIGNED(in->ctx.data.len, 4)", added)
        self.assertIn("ioread8_rep", added)
        self.assertIn("else", added)

    def test_word_count_is_not_byte_count(self):
        self.assertIn("in->ctx.data.len / 4", PATCH.read_text())
        self.assertIn("ioread32_rep", PATCH.read_text())

    def test_no_write_or_ecc_change(self):
        changed = "\n".join(line[1:] for line in PATCH.read_text().splitlines()
                            if line[:1] in ("+", "-") and line[:3] not in ("+++", "---"))
        self.assertNotIn("iowrite", changed)
        self.assertNotIn("writel", changed)
        self.assertNotIn("ecc.", changed)

if __name__ == "__main__":
    unittest.main()

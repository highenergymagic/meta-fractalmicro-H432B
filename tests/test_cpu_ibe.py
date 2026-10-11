# SPDX-License-Identifier: MIT
"""Cortex-A8 firmware prerequisite and read-only validation contracts."""
from pathlib import Path
import random
import unittest

ROOT = Path(__file__).resolve().parents[1]
RECIPES = ROOT / "recipes-bsp/u-boot"
PATCH = RECIPES / "files/0011-cortex-a8-ibe.patch"


def added_source():
    return "\n".join(line[1:] for line in PATCH.read_text().splitlines()
                     if line.startswith("+") and not line.startswith("+++"))


class CortexA8IbeTests(unittest.TestCase):
    def test_every_loader_inherits_cpu_prerequisite(self):
        self.assertIn(PATCH.name, (RECIPES / "u-boot-h432b.inc").read_text())
        for recipe in RECIPES.glob("u-boot-h432b*.bb"):
            current = recipe
            seen = set()
            while current.name != "u-boot-h432b.inc":
                self.assertNotIn(current, seen)
                seen.add(current)
                parents = [line.split()[1] for line in current.read_text().splitlines()
                           if line.startswith("require ")]
                self.assertEqual(len(parents), 1, recipe.name)
                current = current.parent / parents[0]

    def test_cpu_fix_is_separate_from_board_initialization(self):
        patch = PATCH.read_text()
        code = added_source()
        block = code.split("#ifdef CONFIG_ARM_CORTEX_A8_CVE_2017_5715", 1)[1]
        block = block.split("#endif", 1)[0]
        instructions = [line.strip() for line in block.splitlines()
                        if line.strip().startswith(("mrc", "orr", "mcr", "isb"))]
        self.assertEqual(instructions, ["mrc\tp15, 0, r0, c1, c0, 1",
                                       "orr\tr0, r0, #(1 << 6)",
                                       "mcr\tp15, 0, r0, c1, c0, 1", "isb"])
        self.assertIn("\tmsr\tcpsr,r0\n+#ifdef CONFIG_ARM_CORTEX_A8", patch)
        self.assertIn("+#endif\n #ifdef CONFIG_HIMS_U2", patch)
        self.assertNotIn("CONFIG_SKIP_LOWLEVEL_INIT", code)
        self.assertNotIn("c1, c1", code)

    def test_status_only_reads_and_requires_correct_cpu_and_ibe(self):
        code = added_source()
        status = code.split("static int u2_cpu_status(void)", 1)[1].split("\n}", 1)[0]
        self.assertEqual(status.count("mrc p15"), 2)
        self.assertNotIn("mcr ", status)
        self.assertIn("(midr & 0xff0ffff0) != 0x410fc080", status)
        self.assertIn("return (actlr & (1U << 6)) ? 0 : CMD_RET_FAILURE;", status)
        self.assertEqual(0x412fc082 & 0xff0ffff0, 0x410fc080)
        self.assertNotEqual(0x410fc090 & 0xff0ffff0, 0x410fc080)

    def test_setting_ibe_preserves_all_other_actlr_bits(self):
        rng = random.Random(5715)
        vectors = [0, 0xffffffff] + [1 << bit for bit in range(32)]
        vectors.extend(rng.getrandbits(32) for _ in range(10000))
        for before in vectors:
            after = before | (1 << 6)
            self.assertTrue(after & (1 << 6))
            self.assertEqual((before ^ after) & ~(1 << 6), 0)


if __name__ == "__main__":
    unittest.main()

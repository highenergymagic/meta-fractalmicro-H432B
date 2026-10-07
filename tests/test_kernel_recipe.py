# SPDX-License-Identifier: MIT
"""Small regression checks for the tarball kernel integration contract."""
import re
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
RECIPE = ROOT / "recipes-kernel/linux/linux-h432b-base.inc"


class KernelRecipe(unittest.TestCase):
    def test_source_override_follows_kernel_class(self):
        text = RECIPE.read_text()
        self.assertGreater(
            text.index('S = "${UNPACKDIR}/linux-cip-6.12.111-cip32"'),
            text.index("inherit kernel h432b-build-identity"),
        )

    def test_rt_metadata_preserves_payload_and_is_idempotent(self):
        text = RECIPE.read_text()
        hook = re.search(
            r"python do_patch:prepend\(\) \{\n(.*?)\n\}", text, re.S
        ).group(1)
        import textwrap
        import tempfile
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "patch-6.12.111-rt21.patch"
            payload = b"diff --git a/test b/test\n--- a/test\n+++ b/test\n"
            path.write_bytes(payload)

            class Data:
                def getVar(self, key):
                    assert key == "UNPACKDIR"
                    return folder

            exec(textwrap.dedent(hook), {"d": Data()})
            first = path.read_bytes()
            self.assertTrue(first.endswith(payload))
            self.assertIn(b"Upstream-Status: Inappropriate [", first)
            self.assertIn(b"Source: https://www.kernel.org/", first)
            exec(textwrap.dedent(hook), {"d": Data()})
            self.assertEqual(path.read_bytes(), first)

    def test_uboot_uses_wrynose_git_source_layout(self):
        text = (ROOT / "recipes-bsp/u-boot/u-boot-h432b.inc").read_text()
        self.assertNotIn('S = "${UNPACKDIR}/git"', text)
        self.assertIn('B = "${S}"', text)

    def test_kernel_uses_oe_toolchain(self):
        text = RECIPE.read_text()
        self.assertNotIn("H432B_CROSS", text)
        self.assertNotIn("DEPENDS:remove", text)
        self.assertIn("inherit kernel h432b-build-identity", text)

    def test_uboot_uses_oe_toolchain_and_explicit_language(self):
        text = (ROOT / "recipes-bsp/u-boot/u-boot-h432b.inc").read_text()
        self.assertIn("CROSS_COMPILE=${TARGET_PREFIX}", text)
        self.assertIn("-std=gnu89", text)
        self.assertIn('DEPENDS += "libgcc"', text)
        self.assertNotIn("INHIBIT_DEFAULT_DEPS", text)
        self.assertNotIn("H432B_CROSS", text)

    def test_gcc_patch_sequences_all_three_shift_variants(self):
        append = (ROOT / "recipes-devtools/gcc/gcc-source_15.3.bbappend").read_text()
        self.assertIn("0001-arm-sequence-shift-scratch-registers.patch", append)
        patch = (ROOT / "recipes-devtools/gcc/files/0001-arm-sequence-shift-scratch-registers.patch").read_text()
        self.assertEqual(patch.count("+  rtx scratch0 = gen_reg_rtx (SImode);"), 3)
        self.assertEqual(patch.count("+  rtx scratch1 = gen_reg_rtx (SImode);"), 3)
        for op in ("ASHIFT", "ASHIFTRT", "LSHIFTRT"):
            self.assertIn("arm_emit_coreregs_64bit_shift (" + op, patch)

    def test_xz_ram_root_and_fixed_artifact_names(self):
        text = RECIPE.read_text()
        self.assertIn('IMAGE_VERSION_SUFFIX = "-${SOURCE_DATE_EPOCH}"', text)
        self.assertIn("PREEMPT_RT RD_XZ", text)
        config = (ROOT / "recipes-kernel/linux/files/u2-ram.config").read_text()
        self.assertIn("CONFIG_RD_XZ=y", config)

    def test_upstream_archives_remain_checksum_pinned(self):
        text = RECIPE.read_text()
        for source in ("kernel", "rt"):
            self.assertRegex(
                text, r'SRC_URI\[' + source + r'\.sha256sum\] = "[0-9a-f]{64}"'
            )


if __name__ == "__main__":
    unittest.main()

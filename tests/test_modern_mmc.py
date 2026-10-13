# SPDX-License-Identifier: MIT
"""Source-boundary checks; device readback is a separate qualification."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1] / 'recipes-bsp/u-boot'


class ModernMmcTests(unittest.TestCase):
    def setUp(self):
        self.patch = (ROOT / 'files/0018-h432b-internal-sd.patch').read_text()

    def test_only_internal_controller(self):
        self.assertIn('dev_read_addr(dev) != H432B_HSMMC1', self.patch)
        self.assertIn('#define H432B_HSMMC1 0xeb100000', self.patch)
        self.assertIn('mmc0 = &internal_sd', self.patch)
        self.assertNotIn('mmc@eb200000', self.patch)
        self.assertNotIn('mmc@eb300000', self.patch)

    def test_protocol_is_upstream_and_no_dma(self):
        self.assertIn('return sdhci_probe(dev)', self.patch)
        self.assertIn('.ops = &sdhci_ops', self.patch)
        self.assertIn('# CONFIG_MMC_SDHCI_SDMA is not set', self.patch)
        self.assertIn('# CONFIG_MMC_SDHCI_ADMA is not set', self.patch)

    def test_probe_never_writes_media(self):
        self.assertIn('blk_dread(desc, 0, 2048, buffer) != 2048', self.patch)
        self.assertNotIn('blk_dwrite', self.patch)
        self.assertNotIn('blk_derase', self.patch)
        self.assertIn('desc->blksz != 512', self.patch)
        self.assertIn('sha256_csum_wd', self.patch)

    def test_explicit_qualification_target(self):
        recipe = (ROOT / 'u-boot-h432b-modern-mmc-test_2026.10.bb').read_text()
        self.assertIn('modern-nand-mmc-test', recipe)
        self.assertIn('h432bmmc; h432bstorage; fastboot usb 0', recipe)
        self.assertNotIn('PROVIDES', recipe)

    def test_extension_backend_is_internal_gpt_only(self):
        patch = (ROOT / 'files/0020-h432b-systemext-fastboot.patch').read_text()
        self.assertIn('dev_read_addr(mmc->dev) != 0xeb100000', patch)
        self.assertIn('uclass_get_device_by_seq(UCLASS_MMC, 0, &dev)', patch)
        self.assertIn('blk_get_device(UCLASS_MMC, 0, &block)', patch)
        self.assertIn('dev_get_parent(block) != dev', patch)
        self.assertIn('disk->part_type != PART_TYPE_EFI', patch)
        self.assertIn('strcmp(part.name, labels[i])', patch)
        self.assertIn('part.start != starts[i]', patch)
        self.assertIn('"systemext_A", "systemext_B", "cache", "userdata"', patch)
        self.assertIn('slot > 1', patch)
        self.assertNotIn('blk_derase', patch)

    def test_extension_is_verified_before_activation(self):
        patch = (ROOT / 'files/0020-h432b-systemext-fastboot.patch').read_text()
        self.assertIn('fractalmicro,systemext-verity-v1', patch)
        self.assertIn('h432b_extension_verify(slot, &extension)', patch)
        self.assertIn('h432b_verity_stream(verity', patch)
        self.assertIn('blkcache_invalidate', patch)
        self.assertIn('memcmp(expected, actual, sizeof(expected))', patch)
        self.assertIn('Extension verified; slot not activated', patch)
        self.assertIn('Expected raw EROFS image and internal SD v1 layout', patch)


if __name__ == '__main__':
    unittest.main()

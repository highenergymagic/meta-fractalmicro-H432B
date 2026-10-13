# SPDX-License-Identifier: MIT
"""Source contracts for modern loader image roles and storage boundaries."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
RECIPES = ROOT / 'recipes-bsp/u-boot'


class ModernUboot(unittest.TestCase):
    def test_read_performance_contract(self):
        patch = (RECIPES / 'files/0012-h432b-modern-read-performance.patch').read_text()
        self.assertIn('-\tret = verify_kernel(state.order[0]);', patch)
        for field in ('attach', 'kernel', 'hash', 'total'):
            self.assertIn('openh432.loader_' + field + '_ms=%lu', patch)
        self.assertIn('config BCH_CONST_PARAMS', patch)
        self.assertIn('default 13', patch)
        self.assertIn('default 8', patch)
        recipe = (RECIPES / 'u-boot-h432b-modern_2026.10.bb').read_text()
        self.assertIn('modern/check-bch-equivalence.py --source ${S}', recipe)

    def test_nand_role_is_explicit(self):
        recipe = (RECIPES / 'u-boot-h432b-modern-nand_2026.10.bb').read_text()
        self.assertIn('require u-boot-h432b-modern_2026.10.bb', recipe)
        self.assertIn('H432B_MODERN_ENTRY = "40021000"', recipe)
        self.assertIn('--modern-elf ${B}/u-boot', recipe)
        self.assertIn('h432bauto; fastboot usb 0', recipe)
        self.assertIn('--enable H432B_NAND_BOOT_DCACHE', recipe)
        foundation = (RECIPES / 'u-boot-h432b-modern_2026.10.bb').read_text()
        self.assertIn('0018-h432b-internal-sd.patch', foundation)
        patch = (RECIPES / 'files/0011-h432b-modern-autoboot.patch').read_text()
        self.assertIn('h432b_take_bootmode(mode_read, mode_write', patch)
        self.assertIn('if (ret || mode)', patch)
        self.assertIn('U2_BRAILLE_BRAND_BRF', patch)
        self.assertIn('U2_BRAILLE_KERNEL_BRF', patch)

    def test_getvar_enumeration(self):
        patch = (RECIPES / 'files/0009-h432b-modern-getvar.patch').read_text()
        self.assertIn('fastboot_fail("unrecognised variable", response)', patch)
        self.assertIn('fastboot_getvar(key, value)', patch)
        self.assertIn('current_all_board = 0;', patch)
        self.assertIn('"ERROR: "', patch)
        for name in ('slot-count', 'h432b-nand', 'h432b-ubi',
                     'partition-size:systembase_A', 'partition-size:boot_b'):
            self.assertIn('"' + name + '"', patch)

    def test_linux_handoff(self):
        patch = (RECIPES / 'files/0010-h432b-modern-linux-handoff.patch').read_text()
        self.assertIn('CONFIG_CMD_BOOTZ=y', patch)
        self.assertIn('CONFIG_SUPPORT_RAW_INITRD=y', patch)
        self.assertIn('h432b_bootstate_attempt(&store, scratch)', patch)
        self.assertLess(patch.index('h432b_bootstate_attempt(&store, scratch)'),
                        patch.index('run_command(command, 0)'))
        self.assertIn('layout.kernel_size > 0x01000000', patch)
        self.assertIn('fdt_check_full(buffer + layout.dtb, layout.dtb_size)', patch)
        self.assertIn('h432b_usb_bootargs(', patch)
        self.assertIn('"local-mac-address", factory_mac, 6', patch)
        self.assertIn('openh432.attempt=%u', patch)

    def test_pair_activation_and_partition_queries(self):
        patch = (RECIPES / 'files/0008-h432b-modern-slot-activation.patch').read_text()
        self.assertIn('fastboot_board_part_info(part_name, size, response)', patch)
        self.assertIn('fractalmicro,systembase-verity-v1', patch)
        self.assertIn('env_set("fastboot.slot-count", "2")', patch)
        self.assertIn('fdt_check_full(fdt, layout.dtb_size)', patch)
        self.assertIn('h432b_verity_verify(buffer, vol->used_bytes', patch)
        self.assertLess(patch.index('fastboot_invalidate_image();'),
                        patch.index("if (verify_pair(slot[0] - 'a'))"))
        self.assertLess(patch.index("if (verify_pair(slot[0] - 'a'))"),
                        patch.index('ret = h432b_fastboot_set_active'))

    def test_modern_nand_callback_integration(self):
        fix = (RECIPES / 'files/0007-h432b-modern-nand-write-callback.patch').read_text()
        recipe = (RECIPES / 'u-boot-h432b-modern_2026.10.bb').read_text()
        self.assertIn('0007-h432b-modern-nand-write-callback.patch', recipe)
        self.assertIn('raw_write_oob = mtd->_write_oob;', fix)
        self.assertIn('ret = raw_write_oob(mtd, offset, &ops);', fix)
        self.assertIn('*done = ops.retlen;', fix)
        self.assertIn('if (!mtd->_write_oob || !mtd->_erase || !mtd->_block_markbad)', fix)
        self.assertIn('.mode = MTD_OPS_PLACE_OOB,', fix)

    def test_cached_boot_restores_usb_contract(self):
        patch = (RECIPES / 'files/0013-h432b-modern-cached-boot.patch').read_text()
        recipe = (RECIPES / 'u-boot-h432b-modern_2026.10.bb').read_text()
        self.assertIn('file://0013-h432b-modern-cached-boot.patch', recipe)
        self.assertIn('+# CONFIG_SYS_DCACHE_OFF is not set', patch)
        cache = patch.split('+++ b/board/fractalmicro/h432b/cache.c', 1)[1]
        cache = cache.split('diff --git', 1)[0]
        self.assertLess(cache.index('g_dnl_unregister();'),
                        cache.index('dcache_enable();'))
        self.assertLess(cache.index('clrbits_le32(OTG_AHB_CONFIG'),
                        cache.index('dcache_enable();'))
        self.assertIn('wait_for_bit_le32(OTG_RESET, BIT(31), true, 100, false)', patch)
        self.assertIn('gd->arch.tlb_addr != 0x44000000 - 0x10000', patch)
        self.assertIn('dcache_disable();', patch)
        self.assertIn('invalidate_dcache_all();', patch)
        self.assertIn('+\th432b_cache_leave();\n \treturn run_command(command', patch)
        self.assertIn('+\t\tret = h432b_boot(cmdtp, flag, argc, argv);\n+\th432b_cache_leave();', patch)
        self.assertIn('+\tif (!mode && h432b_cache_enter())', patch)

    def test_device_only_connector_overrides_inherited_id(self):
        patch = (RECIPES / 'files/0014-h432b-modern-device-mode-reset.patch').read_text()
        self.assertIn('clrsetbits_le32(H432B_OTG_USBCFG, BIT(29), BIT(30));', patch)
        self.assertIn('GUSBCFG_FORCEDEVMODE) &&', patch)
        self.assertIn('file://0014-h432b-modern-device-mode-reset.patch',
                      (RECIPES / 'u-boot-h432b-modern_2026.10.bb').read_text())

    def test_gadget_reset_never_waits_for_host_role(self):
        patch = (RECIPES / 'files/0015-dwc2-reset-caller-role.patch').read_text()
        self.assertIn('+\tret = dwc2_core_reset(reg, false);', patch)
        self.assertEqual(patch.count('+\tdwc2_core_reset(regs, true);'), 3)
        self.assertIn('+\tif (wait_for_host_mode &&', patch)
        self.assertIn('file://0015-dwc2-reset-caller-role.patch',
                      (RECIPES / 'u-boot-h432b-modern_2026.10.bb').read_text())

    def test_unqualified_cache_is_opt_in_without_skipping_usb_stop(self):
        patch = (RECIPES / 'files/0016-h432b-cache-qualification-switch.patch').read_text()
        self.assertIn('+config H432B_NAND_BOOT_DCACHE', patch)
        self.assertIn('+\tdefault n', patch)
        self.assertIn('+\tif (!IS_ENABLED(CONFIG_H432B_NAND_BOOT_DCACHE))', patch)
        self.assertLess(patch.index('wait_for_bit_le32(OTG_RESET'),
                        patch.index('if (!IS_ENABLED(CONFIG_H432B_NAND_BOOT_DCACHE))'))
        self.assertIn('+\tprepared = false;', patch)
        self.assertIn('+\tif (prepared)', patch)

    def test_cache_audit_checks_ram_before_linux_handoff(self):
        patch = (RECIPES / 'files/0017-h432b-cache-handoff-audit.patch').read_text()
        self.assertIn('before[i] = crc32', patch)
        self.assertIn('after = crc32', patch)
        start = patch.index('before[i] = crc32')
        stop = patch.index('after = crc32')
        self.assertIn('h432b_cache_leave();', patch[start:stop])
        self.assertIn('if (before[i] != after)', patch)
        self.assertIn('return -EIO;', patch)
        self.assertIn('openh432.loader_cache_audit=%u', patch)
        recipe = (RECIPES / 'u-boot-h432b-modern-cache-audit_2026.10.bb').read_text()
        self.assertIn('require u-boot-h432b-modern-cache-test_2026.10.bb', recipe)
        self.assertIn('H432B_MODERN_DIR = "modern-nand-cache-audit"', recipe)
        self.assertIn('--enable H432B_CACHE_HANDOFF_AUDIT', recipe)
        cached = (RECIPES / 'u-boot-h432b-modern-cache-test_2026.10.bb').read_text()
        self.assertIn('require u-boot-h432b-modern-nand_2026.10.bb', cached)
        self.assertIn('--enable H432B_NAND_BOOT_DCACHE', cached)
        self.assertNotIn('--enable H432B_CACHE_HANDOFF_AUDIT', cached)

    def test_complete_kernel_read_and_transactional_flash(self):
        verify = (RECIPES / 'files/0005-h432b-modern-kernel-verification.patch').read_text()
        flash = (RECIPES / 'files/0006-h432b-modern-volume-flash.patch').read_text()
        self.assertIn('ubi_eba_read_leb(ubi_devices[0]', verify)
        self.assertIn('0, length, 1)', verify)
        self.assertIn('u2_fb_layout(buffer, size, &layout)', verify)
        self.assertIn('sha256_csum_wd(buffer, size', verify)
        self.assertIn('h432b_nand_write_allowed(offset, length, 2048)', flash)
        self.assertIn('mtd->_write_oob = deny_oob', flash)
        self.assertIn('active == target.slot', flash)
        self.assertLess(flash.index('ret = h432b_fastboot_invalidate'),
                        flash.index('ret = ubi_volume_write(target.name'))
        self.assertIn('volume_digest(vol, actual)', flash)
        self.assertIn('memcmp(expected, actual', flash)
        self.assertIn('slot not activated', flash)
        self.assertIn('CONFIG_FASTBOOT_BUF_SIZE=0x07000000', flash)

    def test_linux_nand_format_and_shared_state(self):
        patch = (RECIPES / 'files/0004-h432b-modern-nand-reader.patch').read_text()
        recipe = (RECIPES / 'u-boot-h432b-modern_2026.10.bb').read_text()
        self.assertIn('file://bootstate.h', recipe)
        self.assertIn('select NAND_ECC_BCH if MTD_RAW_NAND', patch)
        self.assertIn('select BCH if MTD_RAW_NAND', patch)
        self.assertIn('+\tnand_init();', patch)
        self.assertIn('Missing required modern NAND feature', recipe)
        self.assertIn('chip->ecc.strength = 8;', patch)
        self.assertIn('chip->ecc.size = 512;', patch)
        self.assertIn('chip->ecc.layout->eccpos[0] != 12', patch)
        self.assertIn('NAND_SKIP_BBTSCAN', patch)
        self.assertIn('mtd->flags &= ~MTD_WRITEABLE;', patch)
        self.assertIn('4m(factory)ro,507m(linux),1m(bbt)ro', patch)
        self.assertIn('vol->updating || vol->upd_marker || vol->corrupted', patch)
        self.assertIn('h432b_bootstate_load(&store', patch)
        self.assertIn('vol->reserved_pebs != 2', patch)
        self.assertNotIn('ubi_volume_write(', patch)

    def test_ram_layout_is_below_launching_loader(self):
        patch = (RECIPES / 'files/0003-h432b-modern-ram-layout.patch').read_text()
        self.assertIn('+CONFIG_TEXT_BASE=0x42000000', patch)
        self.assertIn('+CONFIG_CUSTOM_SYS_INIT_SP_ADDR=0x41f00000', patch)
        self.assertIn('+\treturn 0x44000000;', patch)

    def test_pinned_opt_in_recipe(self):
        recipe = (RECIPES / 'u-boot-h432b-modern_2026.10.bb').read_text()
        self.assertIn('5508406582f6f7120dce0c4b3059819a41788db2', recipe)
        self.assertIn('0001-h432b-modern-foundation.patch', recipe)
        self.assertIn('inherit deploy h432b-build-identity', recipe)
        self.assertIn('B = "${WORKDIR}/build"', recipe)
        self.assertIn('${H432B_MODERN_ENTRY} T _start', recipe)
        self.assertIn('H432B_MODERN_ENTRY = "42000000"', recipe)
        self.assertIn('modern-ram-fastboot', recipe)
        self.assertNotIn('PROVIDES', recipe)
        for config in (ROOT / 'conf').rglob('*.conf'):
            self.assertNotIn('u-boot-h432b-modern', config.read_text())

    def test_factory_dram_preserved(self):
        patch = (RECIPES / 'files/0001-h432b-modern-foundation.patch').read_text()
        self.assertIn('CONFIG_SKIP_LOWLEVEL_INIT_ONLY=y', patch)
        self.assertIn('CFG_SYS_SDRAM_BASE 0x40000000', patch)
        self.assertIn('CFG_SYS_SDRAM_SIZE 0x10000000', patch)
        self.assertIn('CONFIG_ENV_IS_NOWHERE=y', patch)
        self.assertNotIn('MAX8998', patch)
        self.assertNotIn('CONFIG_ENV_IS_IN_MMC=y', patch)
        self.assertNotIn('CONFIG_ENV_IS_IN_NAND=y', patch)

    def test_transport_has_explicit_board_phy_and_window(self):
        patch = (RECIPES / 'files/0002-h432b-modern-usb.patch').read_text()
        self.assertIn('CONFIG_USB_FUNCTION_FASTBOOT=y', patch)
        self.assertIn('CONFIG_FASTBOOT_BUF_ADDR=0x48000000', patch)
        self.assertIn('CONFIG_FASTBOOT_BUF_SIZE=0x02000000', patch)
        self.assertIn('CONFIG_BOOTCOMMAND="fastboot usb 0"', patch)
        self.assertIn('# CONFIG_FASTBOOT_FLASH is not set', patch)
        self.assertIn('Braille Sense U2 (Maintenance)', patch)
        self.assertIn('Fractal Microsystems', patch)
        self.assertIn('0xe010e80c', patch)
        self.assertIn('0xec100000', patch)
        self.assertNotIn('MAX8998', patch)

    def test_unimplemented_slots_do_not_lie(self):
        patch = (RECIPES / 'files/0002-h432b-modern-usb.patch').read_text()
        additions = '\n'.join(line[1:] for line in patch.splitlines()
                              if line.startswith('+') and not line.startswith('+++'))
        self.assertIn('.dispatch = fastboot_set_active', additions)
        self.assertIn('fastboot_fail("Slot activation is not available", response)', additions)
        self.assertIn('fastboot_fail("Persistent slot state is not available", response)', additions)
        self.assertNotIn('fastboot_okay("a"', additions)

    def test_core_reset_failure_propagates(self):
        patch = (RECIPES / 'files/0002-h432b-modern-usb.patch').read_text()
        self.assertIn('+\tret = dwc2_core_reset(reg);', patch)
        self.assertIn('+\tret = reconfig_usbd(dev);', patch)
        self.assertIn('+\tretval = udc_enable(dev);', patch)
        self.assertIn('+\t\tdisable_irq(IRQ_OTG);', patch)
        self.assertIn('+\t\tif (driver->unbind)', patch)


if __name__ == '__main__':
    unittest.main()

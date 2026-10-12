# SPDX-License-Identifier: MIT
require u-boot-h432b-modern-nand_2026.10.bb

SUMMARY = "H432B cached NAND loader for runtime qualification"
H432B_MODERN_DIR = "modern-nand-cache-test"

do_configure:append() {
    ${S}/scripts/config --file ${B}/.config --enable H432B_NAND_BOOT_DCACHE
    oe_runmake -C ${S} olddefconfig
    grep -qx 'CONFIG_H432B_NAND_BOOT_DCACHE=y' ${B}/.config || bbfatal "Cache test requires caching"
}

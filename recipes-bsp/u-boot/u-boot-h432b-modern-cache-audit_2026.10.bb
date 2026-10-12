# SPDX-License-Identifier: MIT
require u-boot-h432b-modern-cache-test_2026.10.bb

SUMMARY = "H432B diagnostic cached NAND loader with handoff RAM comparison"
H432B_MODERN_DIR = "modern-nand-cache-audit"

do_configure:append() {
    ${S}/scripts/config --file ${B}/.config --enable H432B_CACHE_HANDOFF_AUDIT
    oe_runmake -C ${S} olddefconfig
    grep -qx 'CONFIG_H432B_CACHE_HANDOFF_AUDIT=y' ${B}/.config || bbfatal "Cache audit was not enabled"
}

# SPDX-License-Identifier: MIT
require u-boot-h432b-nand_2012.10.bb
SUMMARY = "RAM-only NAND timing and instruction-cache experiment"
H432B_UBOOT_ROLE = "ram57-nand-profile"
H432B_UBOOT_WARNING = "RAM-only NAND profiling; no automatic boot or NAND writes."
do_configure:append() {
    printf '\n#define CONFIG_U2_NAND_PROFILE 1\n' >> ${S}/include/configs/hims_u2.h
}

# SPDX-License-Identifier: MIT
require u-boot-h432b-nand-timer_2012.10.bb
SUMMARY = "RAM-only BCH subpage-read performance experiment"
SRC_URI += "file://0007-bch-subpage-read.patch"
H432B_UBOOT_ROLE = "ram-nand-subpage"
H432B_UBOOT_WARNING = "RAM-only read optimization experiment. Not a CE carrier. NAND writes remain blocked."
do_configure:append() {
    printf '\n#define CONFIG_U2_NAND_SUBPAGE 1\n' >> ${S}/include/configs/hims_u2.h
}

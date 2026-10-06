# SPDX-License-Identifier: MIT
require u-boot-h432b-nand_2012.10.bb
SUMMARY = "H432B RAM second stage: read-only NAND kernel-A autoboot"
SRC_URI += "file://0005-nand-autoboot.patch"
H432B_UBOOT_ROLE = "ram55-nand-autoboot"
H432B_UBOOT_WARNING = "RAM55 at 0x46000000. Read-only NAND boot with USB fallback; not itself a CE carrier."

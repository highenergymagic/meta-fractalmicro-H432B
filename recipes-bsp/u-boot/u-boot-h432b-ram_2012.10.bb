# SPDX-License-Identifier: MIT
require u-boot-h432b.inc
SRC_URI += "file://0001-ram-loader-rev52.patch"
H432B_UBOOT_ROLE = "ram52-only"
H432B_UBOOT_ENTRY = "46000000"
H432B_UBOOT_WARNING = "RAM52 loader at 0x46000000. RAM ONLY. NEVER put this in a NAND/CE update carrier."

# SPDX-License-Identifier: MIT
require u-boot-h432b.inc
PROVIDES += "virtual/bootloader"
H432B_UBOOT_ROLE = "nand51-raw"
H432B_UBOOT_ENTRY = "40021000"
H432B_UBOOT_WARNING = "Raw NAND51-linked code, NOT a CE/factory update carrier. No installer supplied."

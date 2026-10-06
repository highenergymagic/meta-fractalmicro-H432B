# SPDX-License-Identifier: MIT
require u-boot-h432b-chain_2012.10.bb
SUMMARY = "H432B CE carrier with one-shot fastboot and fixed kernel-B selection"
H432B_CHAIN_STAGE_RECIPE = "u-boot-h432b-maintenance"
H432B_CHAIN_STAGE_DIR = "ram-maintenance-b"
H432B_CHAIN_CARRIER_DIR = "nand-maintenance-ce-carrier"
H432B_UBOOT_ROLE = "nand-maintenance-chain-raw"
H432B_UBOOT_WARNING = "Raw bootstrap, NOT directly flashable. Validated CE carrier requires a verified kernel_b volume. Factory bootloader must be retained."

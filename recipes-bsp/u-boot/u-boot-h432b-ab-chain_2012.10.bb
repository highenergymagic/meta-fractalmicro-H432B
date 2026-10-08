# SPDX-License-Identifier: MIT
require u-boot-h432b-chain.inc
SUMMARY = "H432B factory-compatible carrier with persistent A/B selection"
H432B_CHAIN_STAGE_RECIPE = "u-boot-h432b-ab"
H432B_CHAIN_STAGE_DIR = "ram-ab"
H432B_CHAIN_CARRIER_DIR = "nand-ab-ce-carrier"
H432B_UBOOT_ROLE = "nand-ab-chain-raw"
H432B_UBOOT_WARNING = "Raw bootstrap, NOT directly flashable. CE carrier requires initialized redundant bootstate and qualified slot images."

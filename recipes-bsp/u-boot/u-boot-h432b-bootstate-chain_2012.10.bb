# SPDX-License-Identifier: MIT
require u-boot-h432b-chain.inc
SUMMARY = "H432B CE carrier for explicit bootstate qualification"
H432B_CHAIN_STAGE_RECIPE = "u-boot-h432b-bootstate"
H432B_CHAIN_STAGE_DIR = "ram-bootstate"
H432B_CHAIN_CARRIER_DIR = "nand-bootstate-test-ce-carrier"
H432B_UBOOT_ROLE = "nand-bootstate-test-chain-raw"
H432B_UBOOT_WARNING = "Qualification CE carrier: boots a USB shell, not automatic Linux. Retains factory first-stage/EBOOT; explicit Linux-pool bootstate writes are available."

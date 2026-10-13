# SPDX-License-Identifier: MIT
require u-boot-h432b-modern-cache-test_2026.10.bb
SUMMARY = "H432B internal SD read qualification loader"
H432B_MODERN_DIR = "modern-nand-mmc-test"
H432B_MODERN_COMMAND = "h432bmmc; h432bstorage; fastboot usb 0; h432bboot; fastboot usb 0"

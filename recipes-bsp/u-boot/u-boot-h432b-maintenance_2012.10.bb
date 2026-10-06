# SPDX-License-Identifier: MIT
require u-boot-h432b-reboot-test_2012.10.bb
SUMMARY = "H432B one-shot maintenance stage with fixed kernel-B boot policy"
SRC_URI += "file://0009-maintenance-kernel-b.patch"
H432B_UBOOT_ROLE = "ram-maintenance-b"
H432B_UBOOT_WARNING = "High-RAM stage only, not a CE carrier. Boots existing kernel_b; no A/B rollback policy or NAND writes."

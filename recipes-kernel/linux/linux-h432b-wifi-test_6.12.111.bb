# SPDX-License-Identifier: MIT
require linux-h432b-runtime_6.12.111.bb
SUMMARY = "H432B explicit RTL8712 SDIO transport qualification"
H432B_KERNEL_PROVIDER_REMOVE = "virtual/kernel"
KERNEL_PACKAGE_NAME = "kernel-wifi-test"
KERNEL_DEPLOYSUBDIR = "kernel-wifi-test"

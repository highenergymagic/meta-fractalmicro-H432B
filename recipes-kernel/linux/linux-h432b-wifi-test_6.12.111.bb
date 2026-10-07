# SPDX-License-Identifier: MIT
require linux-h432b-runtime_6.12.111.bb
SUMMARY = "H432B explicit RTL8712 SDIO transport qualification"
H432B_KERNEL_PROVIDER_REMOVE = "virtual/kernel"
KERNEL_PACKAGE_NAME = "kernel-wifi-test"
KERNEL_DEPLOYSUBDIR = "kernel-wifi-test"
SRC_URI += "file://0016-wifi-transport.patch file://h432b-wifi-transport.c"
do_configure:append() {
    install -m 0644 ${UNPACKDIR}/h432b-wifi-transport.c ${S}/drivers/mmc/core/
}

# SPDX-License-Identifier: MIT
require linux-h432b-external-sd-test_6.12.111.bb
SUMMARY = "GPS diagnostic markers using the shared NAND runtime GPS support"
KERNEL_PACKAGE_NAME = "kernel-gps-test"
KERNEL_DEVICETREE = "samsung/s5pv210-hims-u2-gps-test.dtb"
SRC_URI += "file://s5pv210-hims-u2-gps-test.dts"
do_configure:append() {
    install -m 0644 ${UNPACKDIR}/s5pv210-hims-u2-gps-test.dts ${S}/arch/arm/boot/dts/samsung/
}

# SPDX-License-Identifier: MIT
require linux-h432b-platform.inc
require h432b-input.inc
SUMMARY = "H432B input diagnostic profile using the runtime driver"
KERNEL_PACKAGE_NAME = "kernel-input-test"
KERNEL_DEVICETREE = "samsung/s5pv210-hims-u2-input-test.dtb"
SRC_URI += "file://s5pv210-hims-u2-input-test.dts"
do_configure:append() {
    install -m 0644 ${UNPACKDIR}/s5pv210-hims-u2-input-test.dts ${S}/arch/arm/boot/dts/samsung/
}

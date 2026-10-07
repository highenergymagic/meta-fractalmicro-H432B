# SPDX-License-Identifier: MIT
require linux-h432b-platform.inc
SUMMARY = "Opt-in H432B keyboard, routing and selector input qualification"
KERNEL_PACKAGE_NAME = "kernel-input-test"
KERNEL_DEVICETREE = "samsung/s5pv210-hims-u2-input-test.dtb"
SRC_URI += "file://0010-h432b-input.patch file://h432b-input.c file://s5pv210-hims-u2-input-test.dts"

do_configure:append() {
    install -m 0644 ${UNPACKDIR}/h432b-input.c ${S}/drivers/input/keyboard/
    install -m 0644 ${UNPACKDIR}/s5pv210-hims-u2-input-test.dts ${S}/arch/arm/boot/dts/samsung/
}

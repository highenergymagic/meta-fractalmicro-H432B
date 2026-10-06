# SPDX-License-Identifier: MIT
require linux-h432b-reboot-test_6.12.111.bb
SUMMARY = "Opt-in read-only H432B battery transport inventory"
KERNEL_PACKAGE_NAME = "kernel-battery-test"
KERNEL_DEVICETREE = "samsung/s5pv210-hims-u2-battery-test.dtb"
SRC_URI += "file://0009-battery-inventory.patch file://h432b-battery-inventory.c file://s5pv210-hims-u2-battery-test.dts"
do_configure:append() {
    install -m 0644 ${UNPACKDIR}/h432b-battery-inventory.c ${S}/drivers/misc/
    install -m 0644 ${UNPACKDIR}/s5pv210-hims-u2-battery-test.dts ${S}/arch/arm/boot/dts/samsung/
    grep -qx '# CONFIG_SUSPEND is not set' ${B}/.config || bbfatal "Battery test must not enable suspend"
}

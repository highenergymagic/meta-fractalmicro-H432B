# SPDX-License-Identifier: MIT
require linux-h432b-platform.inc
require h432b-battery.inc
SUMMARY = "H432B battery diagnostic profile using the runtime driver"
KERNEL_PACKAGE_NAME = "kernel-battery-test"
KERNEL_DEVICETREE = "samsung/s5pv210-hims-u2-battery-test.dtb"
SRC_URI += "file://s5pv210-hims-u2-battery-test.dts file://h432b-battery-debug.config"
do_configure:append() {
    install -m 0644 ${UNPACKDIR}/s5pv210-hims-u2-battery-test.dts ${S}/arch/arm/boot/dts/samsung/
    KCONFIG_CONFIG=${B}/.config ${S}/scripts/kconfig/merge_config.sh -m -O ${B} \
        ${B}/.config ${UNPACKDIR}/h432b-battery-debug.config
    oe_runmake -C ${S} O=${B} olddefconfig
    grep -qx 'CONFIG_H432B_BATTERY_DEBUG=y' ${B}/.config || bbfatal "Missing battery diagnostics"
}

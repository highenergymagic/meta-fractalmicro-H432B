# SPDX-License-Identifier: MIT
require linux-h432b-platform.inc
SUMMARY = "Opt-in H432B PMIC bus inventory kernel"
KERNEL_PACKAGE_NAME = "kernel-power-test"
KERNEL_DEVICETREE = "samsung/s5pv210-hims-u2-power-test.dtb"
SRC_URI += "file://s5pv210-hims-u2-pmic-bus.dtsi file://s5pv210-hims-u2-power-test.dts file://u2-power-test.config"
do_configure:append() {
    install -m 0644 ${UNPACKDIR}/s5pv210-hims-u2-pmic-bus.dtsi ${S}/arch/arm/boot/dts/samsung/
    install -m 0644 ${UNPACKDIR}/s5pv210-hims-u2-power-test.dts ${S}/arch/arm/boot/dts/samsung/
    KCONFIG_CONFIG=${B}/.config ${S}/scripts/kconfig/merge_config.sh -m -O ${B} \
        ${B}/.config ${UNPACKDIR}/u2-power-test.config
    oe_runmake -C ${S} O=${B} olddefconfig
    for option in I2C I2C_CHARDEV I2C_GPIO; do
        grep -qx "CONFIG_$option=y" ${B}/.config || bbfatal "Missing PMIC inventory option: $option"
    done
}

# SPDX-License-Identifier: MIT
require linux-h432b-reboot-test_6.12.111.bb
SUMMARY = "H432B runtime kernel with writable Linux storage and GPS"
# Unlike its historical diagnostic parent, this is the default provider.
H432B_KERNEL_PROVIDER_REMOVE = ""
KERNEL_PACKAGE_NAME = "kernel"
KERNEL_DEPLOYSUBDIR = "kernel-runtime"
KERNEL_DEVICETREE = "samsung/s5pv210-hims-u2-runtime.dtb"
SRC_URI += "file://s5pv210-hims-u2-pmic-bus.dtsi file://s5pv210-hims-u2-runtime.dts file://u2-power-test.config"
do_configure:append() {
    install -m 0644 ${UNPACKDIR}/s5pv210-hims-u2-pmic-bus.dtsi ${S}/arch/arm/boot/dts/samsung/
    install -m 0644 ${UNPACKDIR}/s5pv210-hims-u2-runtime.dts ${S}/arch/arm/boot/dts/samsung/
    KCONFIG_CONFIG=${B}/.config ${S}/scripts/kconfig/merge_config.sh -m -O ${B} \
        ${B}/.config ${UNPACKDIR}/u2-power-test.config
    oe_runmake -C ${S} O=${B} olddefconfig
    for option in I2C I2C_CHARDEV I2C_GPIO; do
        grep -qx "CONFIG_$option=y" ${B}/.config || bbfatal "Missing PMIC bus option: $option"
    done
}

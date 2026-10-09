# SPDX-License-Identifier: MIT
require linux-h432b-platform.inc
SUMMARY = "H432B integrated NAND runtime kernel"
# The runtime is the sole default kernel provider.
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

require h432b-wifi.inc

require h432b-bluetooth.inc

require h432b-fm.inc

require h432b-braille.inc
require h432b-input.inc
require h432b-battery.inc
require h432b-usb-host.inc
require h432b-external-sd.inc

SRC_URI += "file://0027-h432b-audio-capture.patch file://0028-h432b-audio-jacks.patch file://s5pv210-hims-u2-audio-input.dtsi"
do_configure:append() {
    install -m 0644 ${UNPACKDIR}/s5pv210-hims-u2-audio-input.dtsi ${S}/arch/arm/boot/dts/samsung/
}

require h432b-compass.inc
require h432b-suspend.inc

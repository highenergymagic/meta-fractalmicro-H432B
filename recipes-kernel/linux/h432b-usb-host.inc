# SPDX-License-Identifier: MIT
# Shared host PHY lifecycle, external hub power/reset and adapter support.
SRC_URI += "file://0012-onboard-h432b-usb-hub.patch file://s5pv210-hims-u2-usb-host-test.dtsi file://u2-usb-host-test.config file://0011-usb-host-phy-lifecycle.patch"
do_configure:append() {
    install -m 0644 ${UNPACKDIR}/s5pv210-hims-u2-usb-host-test.dtsi ${S}/arch/arm/boot/dts/samsung/
    KCONFIG_CONFIG=${B}/.config ${S}/scripts/kconfig/merge_config.sh -m -O ${B} ${B}/.config ${UNPACKDIR}/u2-usb-host-test.config
    oe_runmake -C ${S} O=${B} olddefconfig
    for option in REGULATOR_FIXED_VOLTAGE USB_ONBOARD_DEV USB_SERIAL_PL2303; do
        grep -qx "CONFIG_$option=y" ${B}/.config || bbfatal "Missing USB host option: $option"
    done
}

# SPDX-License-Identifier: MIT
require linux-h432b-power-test_6.12.111.bb
SUMMARY = "Opt-in H432B reserved factory wake trampoline experiment"
KERNEL_PACKAGE_NAME = "kernel-resume-test"
KERNEL_DEVICETREE = "samsung/s5pv210-hims-u2-resume-test.dtb"
SRC_URI += "file://0012-onboard-h432b-usb-hub.patch file://s5pv210-hims-u2-usb-host-test.dtsi file://u2-usb-host-test.config file://0011-usb-host-phy-lifecycle.patch file://0009-factory-resume-test.patch file://s5pv210-hims-u2-resume-test.dts file://u2-resume-test.config"
do_configure:append() {
    install -m 0644 ${UNPACKDIR}/s5pv210-hims-u2-usb-host-test.dtsi ${S}/arch/arm/boot/dts/samsung/
    install -m 0644 ${UNPACKDIR}/s5pv210-hims-u2-resume-test.dts ${S}/arch/arm/boot/dts/samsung/
    KCONFIG_CONFIG=${B}/.config ${S}/scripts/kconfig/merge_config.sh -m -O ${B} \
        ${B}/.config ${UNPACKDIR}/u2-resume-test.config ${UNPACKDIR}/u2-usb-host-test.config
    oe_runmake -C ${S} O=${B} olddefconfig
    for option in REGULATOR_FIXED_VOLTAGE USB_ONBOARD_DEV USB_SERIAL_PL2303 SUSPEND PM_SLEEP_DEBUG ARM_PATCH_PHYS_VIRT AUTO_ZRELADDR; do
        grep -qx "CONFIG_$option=y" ${B}/.config || bbfatal "Missing resume experiment option: $option"
    done
    if grep -Eq '^CONFIG_PM_TEST_SUSPEND=(y|m)$' ${B}/.config; then
        bbfatal "Automatic suspend is forbidden"
    fi
}

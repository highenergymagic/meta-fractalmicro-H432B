# SPDX-License-Identifier: MIT
require linux-h432b-base.inc
SUMMARY = "H432B NAND runtime kernel with reboot mode and qualified GPS support"
SRC_URI += "file://s5pv210-hims-u2-reboot-test.dts file://u2-reboot-test.config file://s5pv210-hims-u2-gps.dtsi \
    file://h432b-gps-power.c file://0014-h432b-gps-power.patch file://0014a-h432b-gps-binding.patch file://u2-gps.config"

do_configure:append() {
    install -m 0644 ${UNPACKDIR}/h432b-gps-power.c ${S}/drivers/misc/
    install -m 0644 ${UNPACKDIR}/s5pv210-hims-u2-gps.dtsi ${S}/arch/arm/boot/dts/samsung/
    install -m 0644 ${UNPACKDIR}/s5pv210-hims-u2-reboot-test.dts ${S}/arch/arm/boot/dts/samsung/
    KCONFIG_CONFIG=${B}/.config ${S}/scripts/kconfig/merge_config.sh -m -O ${B} \
        ${B}/.config ${UNPACKDIR}/u2-reboot-test.config ${UNPACKDIR}/u2-gps.config
    oe_runmake -C ${S} O=${B} olddefconfig
    for option in REBOOT_MODE SYSCON_REBOOT_MODE H432B_GPS_POWER REGULATOR_FIXED_VOLTAGE SERIAL_SAMSUNG; do
        grep -qx "CONFIG_$option=y" ${B}/.config ||
            bbfatal "Missing platform option: $option"
    done
}

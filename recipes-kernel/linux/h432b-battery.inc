# SPDX-License-Identifier: MIT
# Shared read-only power_supply interface; does not enable sleep or charging control.
SRC_URI += "file://0009-battery-inventory.patch file://h432b-battery-inventory.c file://h432b-battery-policy.h file://test-battery-policy.c file://u2-battery-test.config file://s5pv210-hims-u2-battery.dtsi"
do_configure:append() {
    install -m 0644 ${UNPACKDIR}/h432b-battery-inventory.c ${UNPACKDIR}/h432b-battery-policy.h ${S}/drivers/misc/
    install -m 0644 ${UNPACKDIR}/s5pv210-hims-u2-battery.dtsi ${S}/arch/arm/boot/dts/samsung/
    KCONFIG_CONFIG=${B}/.config ${S}/scripts/kconfig/merge_config.sh -m -O ${B} ${B}/.config ${UNPACKDIR}/u2-battery-test.config
    oe_runmake -C ${S} O=${B} olddefconfig
    grep -qx "CONFIG_POWER_SUPPLY=y" ${B}/.config || bbfatal "Missing power_supply core"
    grep -qx '# CONFIG_SUSPEND is not set' ${B}/.config || bbfatal "Battery/runtime profile must not enable suspend"
}
do_compile:prepend() {
    ${BUILD_CC} -std=c99 -Wall -Wextra -Werror -I${UNPACKDIR} \
        ${UNPACKDIR}/test-battery-policy.c -o ${B}/test-battery-policy
    ${B}/test-battery-policy
}

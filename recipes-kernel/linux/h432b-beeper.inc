# SPDX-License-Identifier: MIT
SRC_URI += "file://h432b-beeper.config file://s5pv210-hims-u2-beeper.dtsi"
SRC_URI += "file://0032-pwm-samsung-honor-disabled-state.patch"
do_configure:append() {
    install -m 0644 ${UNPACKDIR}/s5pv210-hims-u2-beeper.dtsi ${S}/arch/arm/boot/dts/samsung/
    KCONFIG_CONFIG=${B}/.config ${S}/scripts/kconfig/merge_config.sh -m -O ${B} \
        ${B}/.config ${UNPACKDIR}/h432b-beeper.config
    oe_runmake -C ${S} O=${B} olddefconfig
    for option in PWM PWM_SAMSUNG INPUT_MISC INPUT_PWM_BEEPER; do
        grep -qx "CONFIG_$option=y" ${B}/.config || bbfatal "Missing beeper option: $option"
    done
}

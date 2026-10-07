# SPDX-License-Identifier: MIT
SRC_URI += "file://u2-braille.config file://0019-h432b-braille.patch file://0020-samsung-gpio-direction-readback.patch file://h432b-braille.c file://h432b-braille-wire.h file://s5pv210-hims-u2-braille.dtsi"
do_configure:append() {
    KCONFIG_CONFIG=${B}/.config ${S}/scripts/kconfig/merge_config.sh -m -O ${B} ${B}/.config ${UNPACKDIR}/u2-braille.config
    oe_runmake -C ${S} O=${B} olddefconfig
    grep -qx 'CONFIG_INPUT_UINPUT=y' ${B}/.config || bbfatal "Missing BRLTTY input injection"
    install -m 0644 ${UNPACKDIR}/h432b-braille.c ${UNPACKDIR}/h432b-braille-wire.h ${S}/drivers/misc/
    install -m 0644 ${UNPACKDIR}/s5pv210-hims-u2-braille.dtsi ${S}/arch/arm/boot/dts/samsung/
}

# SPDX-License-Identifier: MIT
SRC_URI += "file://0018-h432b-fm-radio.patch file://h432b-fm.c file://s5pv210-hims-u2-fm.dtsi file://h432b-fm.config"
do_configure:append() {
    install -m 0644 ${UNPACKDIR}/h432b-fm.c ${S}/drivers/media/radio/si470x/
    install -m 0644 ${UNPACKDIR}/s5pv210-hims-u2-fm.dtsi ${S}/arch/arm/boot/dts/samsung/
    KCONFIG_CONFIG=${B}/.config ${S}/scripts/kconfig/merge_config.sh -m -O ${B} \
        ${B}/.config ${UNPACKDIR}/h432b-fm.config
    oe_runmake -C ${S} O=${B} olddefconfig
    for option in MEDIA_SUPPORT VIDEO_DEV RADIO_SI470X I2C_SI470X; do
        grep -qx "CONFIG_$option=y" ${B}/.config || bbfatal "Missing FM option: $option"
    done
}

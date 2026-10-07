# SPDX-License-Identifier: MIT
SRC_URI += "file://h432b-bluetooth.config file://s5pv210-hims-u2-bluetooth.dtsi"
do_configure:append() {
    install -m 0644 ${UNPACKDIR}/s5pv210-hims-u2-bluetooth.dtsi ${S}/arch/arm/boot/dts/samsung/
    KCONFIG_CONFIG=${B}/.config ${S}/scripts/kconfig/merge_config.sh -m -O ${B} \
        ${B}/.config ${UNPACKDIR}/h432b-bluetooth.config
    oe_runmake -C ${S} O=${B} olddefconfig
    for option in BT BT_BREDR BT_HCIUART BT_HCIUART_BCSP; do
        grep -qx "CONFIG_$option=y" ${B}/.config || bbfatal "Missing Bluetooth option: $option"
    done
}

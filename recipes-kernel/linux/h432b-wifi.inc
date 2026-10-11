# SPDX-License-Identifier: MIT
H432B_WIFI_CONFIG ?= "h432b-wifi-net.config"
H432B_WIFI_DRIVER_MODE ?= "y"
SRC_URI += " \
    file://0016-wifi-transport.patch \
    file://h432b-wifi-transport.c \
    file://h432b-wifi-power.h \
    file://h432b-wifi-firmware.h \
    file://h432b-wifi-io.h \
    file://h432b-wifi-irq.h \
    file://h432b-wifi-events.h \
    file://h432b-wifi-rx.h \
    file://h432b-wifi-command.h \
    file://h432b-wifi-h2c.h \
    file://h432b-wifi-ccmp.h \
    file://h432b-wifi-data.h \
    file://h432b-wifi-tx.h \
    file://h432b-wifi-ht.h \
    file://h432b-wifi-reorder.h \
    file://h432b-wifi-assoc.h \
    file://h432b-wifi-net.h \
    file://h432b-wifi-stats.h \
    file://h432b-wifi-scan.h \
    file://h432b-wifi-led.h \
    file://h432b-wifi-init.h \
    file://h432b-wifi-debug.h \
    file://${H432B_WIFI_CONFIG} \
    file://s5pv210-hims-u2-wifi.dtsi \
"
do_configure:append() {
    install -m 0644 ${UNPACKDIR}/h432b-wifi-transport.c \
        ${UNPACKDIR}/h432b-wifi-*.h ${S}/drivers/net/wireless/realtek/rtl8712s/
    install -m 0644 ${UNPACKDIR}/s5pv210-hims-u2-wifi.dtsi \
        ${S}/arch/arm/boot/dts/samsung/
    KCONFIG_CONFIG=${B}/.config ${S}/scripts/kconfig/merge_config.sh -m -O ${B} \
        ${B}/.config ${UNPACKDIR}/${H432B_WIFI_CONFIG}
    oe_runmake -C ${S} O=${B} olddefconfig
    grep -qx "CONFIG_CFG80211=y" ${B}/.config || bbfatal "Missing cfg80211"
    grep -qx "CONFIG_RTL8712S=${H432B_WIFI_DRIVER_MODE}" ${B}/.config || bbfatal "Wrong RTL8712S mode"
}

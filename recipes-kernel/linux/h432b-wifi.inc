# SPDX-License-Identifier: MIT
SRC_URI += "file://0016-wifi-transport.patch file://h432b-wifi-transport.c file://h432b-wifi-power.h file://h432b-wifi-firmware.h file://h432b-wifi-irq.h file://h432b-wifi-events.h file://h432b-wifi-rx.h file://h432b-wifi-command.h file://h432b-wifi-h2c.h file://h432b-wifi-ccmp.h file://h432b-wifi-data.h file://h432b-wifi-assoc.h file://h432b-wifi-net.h file://h432b-wifi-net.config"
do_configure:append() {
    install -m 0644 ${UNPACKDIR}/h432b-wifi-transport.c ${UNPACKDIR}/h432b-wifi-power.h ${UNPACKDIR}/h432b-wifi-firmware.h ${UNPACKDIR}/h432b-wifi-irq.h ${UNPACKDIR}/h432b-wifi-events.h ${UNPACKDIR}/h432b-wifi-rx.h ${UNPACKDIR}/h432b-wifi-command.h ${UNPACKDIR}/h432b-wifi-h2c.h ${UNPACKDIR}/h432b-wifi-ccmp.h ${UNPACKDIR}/h432b-wifi-data.h ${UNPACKDIR}/h432b-wifi-assoc.h ${UNPACKDIR}/h432b-wifi-net.h ${S}/drivers/mmc/core/
    KCONFIG_CONFIG=${B}/.config ${S}/scripts/kconfig/merge_config.sh -m -O ${B} \
        ${B}/.config ${UNPACKDIR}/h432b-wifi-net.config
    oe_runmake -C ${S} O=${B} olddefconfig
    grep -qx 'CONFIG_CFG80211=y' ${B}/.config || bbfatal 'cfg80211 missing'
}

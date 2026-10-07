# SPDX-License-Identifier: MIT
SUMMARY = "Operator-supplied factory RTL8712 SDIO firmware"
LICENSE = "CLOSED"
# No binary is distributed or fetched by this layer. Explicit private input only.
H432B_WIFI_FIRMWARE_DIR ?= ""
python __anonymous() {
    if not d.getVar("H432B_WIFI_FIRMWARE_DIR"):
        raise bb.parse.SkipRecipe("Factory Wi-Fi firmware requires an explicit private input")
}
FILESEXTRAPATHS:prepend := "${H432B_WIFI_FIRMWARE_DIR}:"
SRC_URI = "file://rtl8712s.bin"
S = "${UNPACKDIR}"
inherit allarch
do_install() {
    install -d ${D}${nonarch_base_libdir}/firmware/h432b
    install -m 0644 ${UNPACKDIR}/rtl8712s.bin ${D}${nonarch_base_libdir}/firmware/h432b/
}
FILES:${PN} = "${nonarch_base_libdir}/firmware/h432b/rtl8712s.bin"

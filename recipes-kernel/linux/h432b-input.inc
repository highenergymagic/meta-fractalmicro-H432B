# SPDX-License-Identifier: MIT
# Shared keyboard, routing-key and selector support.
SRC_URI += "file://0010-h432b-input.patch file://0010a-h432b-input-binding.patch file://h432b-input.c file://s5pv210-hims-u2-input.dtsi"
do_configure:append() {
    grep -qx "CONFIG_KEYBOARD_H432B=y" ${B}/.config || bbfatal "Missing H432B input driver"
    install -m 0644 ${UNPACKDIR}/h432b-input.c ${S}/drivers/input/keyboard/
    install -m 0644 ${UNPACKDIR}/s5pv210-hims-u2-input.dtsi ${S}/arch/arm/boot/dts/samsung/
}

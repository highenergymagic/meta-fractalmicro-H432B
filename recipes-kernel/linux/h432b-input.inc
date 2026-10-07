# SPDX-License-Identifier: MIT
# Shared keyboard, routing-key and selector support.
SRC_URI += "file://0010-h432b-input.patch file://h432b-input.c file://s5pv210-hims-u2-input.dtsi"
do_configure:append() {
    install -m 0644 ${UNPACKDIR}/h432b-input.c ${S}/drivers/input/keyboard/
    install -m 0644 ${UNPACKDIR}/s5pv210-hims-u2-input.dtsi ${S}/arch/arm/boot/dts/samsung/
}

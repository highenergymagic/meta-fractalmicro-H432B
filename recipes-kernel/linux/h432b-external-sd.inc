# SPDX-License-Identifier: MIT
SRC_URI += "file://s5pv210-hims-u2-external-sd.dtsi"
do_configure:append() {
    install -m 0644 ${UNPACKDIR}/s5pv210-hims-u2-external-sd.dtsi ${S}/arch/arm/boot/dts/samsung/
}

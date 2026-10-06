# SPDX-License-Identifier: MIT
SUMMARY = "H432B Ethernet identification and transient chip-select diagnostics"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"
SRC_URI = "file://ethernet-probe.c file://ethernet-mux-test.c"
S = "${UNPACKDIR}"
COMPATIBLE_MACHINE = "^h432b$"
do_compile() {
    ${CC} ${CFLAGS} -std=c11 -Wall -Wextra -Werror ${LDFLAGS} \
        ${S}/ethernet-probe.c -o ${B}/h432b-ethernet-probe
    ${CC} ${CFLAGS} -std=c11 -Wall -Wextra -Werror ${LDFLAGS} \
        ${S}/ethernet-mux-test.c -o ${B}/h432b-ethernet-mux-test
}
do_install() {
    install -d ${D}${sbindir}
    install -m 0755 ${B}/h432b-ethernet-probe ${B}/h432b-ethernet-mux-test ${D}${sbindir}/
}

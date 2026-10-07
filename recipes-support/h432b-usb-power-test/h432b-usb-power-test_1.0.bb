# SPDX-License-Identifier: MIT
SUMMARY = "Explicit temporary H432B USB board-enable diagnostic"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"
SRC_URI = "file://usb-power-test.c"
S = "${UNPACKDIR}"
COMPATIBLE_MACHINE = "^h432b$"
inherit deploy
do_compile() {
    ${CC} ${CFLAGS} -std=c11 -Wall -Wextra -Werror ${LDFLAGS} \
        ${S}/usb-power-test.c -o ${B}/h432b-usb-power-test
}
do_install() {
    install -d ${D}${sbindir}
    install -m 0755 ${B}/h432b-usb-power-test ${D}${sbindir}/
}
do_deploy() {
    install -d ${DEPLOYDIR}/usb-diagnostics
    install -m 0755 ${B}/h432b-usb-power-test ${DEPLOYDIR}/usb-diagnostics/
}
addtask deploy after do_compile before do_build

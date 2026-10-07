# SPDX-License-Identifier: MIT
SUMMARY = "Explicit H432B braille frame and device-ABI check"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"
SRC_URI = "file://braille-check.c"
S = "${UNPACKDIR}"
COMPATIBLE_MACHINE = "^h432b$"
inherit deploy
do_compile() {
    ${CC} ${CFLAGS} -D_GNU_SOURCE -std=c11 -Wall -Wextra -Werror ${LDFLAGS} \
        ${S}/braille-check.c -o ${B}/h432b-braille-check
}
do_install() {
    install -d ${D}${sbindir}
    install -m 0755 ${B}/h432b-braille-check ${D}${sbindir}/
}
do_deploy() {
    install -d ${DEPLOYDIR}/braille
    install -m 0755 ${B}/h432b-braille-check ${DEPLOYDIR}/braille/
}
addtask deploy after do_compile before do_build

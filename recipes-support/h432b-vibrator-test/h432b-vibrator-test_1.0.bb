# SPDX-License-Identifier: MIT
SUMMARY = "Explicit single-pulse H432B vibration diagnostic"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"
SRC_URI = "file://vibrator-test.c"
S = "${UNPACKDIR}"
COMPATIBLE_MACHINE = "^h432b$"
inherit deploy
do_compile() {
    ${CC} ${CFLAGS} -std=c11 -Wall -Wextra -Werror ${LDFLAGS} \
        ${S}/vibrator-test.c -o ${B}/h432b-vibrator-test
}
do_install() {
    install -d ${D}${sbindir}
    install -m 0755 ${B}/h432b-vibrator-test ${D}${sbindir}/
}
do_deploy() {
    install -d ${DEPLOYDIR}/input-recorder
    install -m 0755 ${B}/h432b-vibrator-test ${DEPLOYDIR}/input-recorder/
}
addtask deploy after do_compile before do_build

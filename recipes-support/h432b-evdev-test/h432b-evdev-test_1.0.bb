# SPDX-License-Identifier: MIT
SUMMARY = "Read-only bounded recorder for H432B evdev qualification"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"
SRC_URI = "file://evdev-test.c"
S = "${UNPACKDIR}"
COMPATIBLE_MACHINE = "^h432b$"
inherit deploy
do_compile() {
    ${CC} ${CFLAGS} -std=c11 -Wall -Wextra -Werror ${LDFLAGS} \
        ${S}/evdev-test.c -o ${B}/h432b-evdev-test
}
do_install() {
    install -d ${D}${sbindir}
    install -m 0755 ${B}/h432b-evdev-test ${D}${sbindir}/
}
do_deploy() {
    install -d ${DEPLOYDIR}/input-recorder
    install -m 0755 ${B}/h432b-evdev-test ${DEPLOYDIR}/input-recorder/
}
addtask deploy after do_compile before do_build

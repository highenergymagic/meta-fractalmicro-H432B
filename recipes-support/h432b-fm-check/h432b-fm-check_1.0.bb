# SPDX-License-Identifier: MIT
SUMMARY = "Explicit muted V4L2 FM tuning qualification"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"
SRC_URI = "file://fm-check.c"
S = "${UNPACKDIR}"
inherit deploy
do_compile() {
    ${CC} ${CFLAGS} ${CPPFLAGS} -Wall -Wextra -Werror fm-check.c ${LDFLAGS} -o h432b-fm-check
}
do_install() {
    install -d ${D}${bindir}
    install -m 0755 h432b-fm-check ${D}${bindir}/
}
do_deploy() {
    install -d ${DEPLOYDIR}
    install -m 0755 h432b-fm-check ${DEPLOYDIR}/
}
addtask deploy after do_compile before do_build

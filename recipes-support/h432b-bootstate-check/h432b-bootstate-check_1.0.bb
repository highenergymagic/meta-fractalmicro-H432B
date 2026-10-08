# SPDX-License-Identifier: MIT
SUMMARY = "H432B bootstate decoder and locked successful-boot update"
DEPENDS = "libubootenv"
LICENSE = "GPL-2.0-or-later"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/GPL-2.0-or-later;md5=fed54355545ffd980b814dab4a3b312c"
FILESEXTRAPATHS:prepend := "${THISDIR}/../../recipes-bsp/u-boot/files/bootstate:"
SRC_URI = "file://bootstate-check.c file://bootstate.h"
S = "${UNPACKDIR}"
inherit deploy
do_compile() {
    ${CC} ${CFLAGS} ${LDFLAGS} -std=gnu99 -Wall -Wextra -Werror -Wno-unused-function \
        -I${S} ${S}/bootstate-check.c -o h432b-bootstate-check -lubootenv
}
do_install() {
    install -d ${D}${bindir}
    install -m 0755 h432b-bootstate-check ${D}${bindir}/
}
do_deploy() {
    install -d ${DEPLOYDIR}/bootstate
    install -m 0755 h432b-bootstate-check ${DEPLOYDIR}/bootstate/
}
addtask deploy after do_compile before do_build

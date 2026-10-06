# SPDX-License-Identifier: MIT
SUMMARY = "Fixed H432B PMIC control register inventory"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"
SRC_URI = "file://power-inventory.c"
S = "${UNPACKDIR}"
COMPATIBLE_MACHINE = "^h432b$"
do_compile() {
    ${CC} ${CFLAGS} -std=c11 -Wall -Wextra -Werror ${LDFLAGS} \
        ${S}/power-inventory.c -o ${B}/h432b-power-inventory
}
do_install() {
    install -d ${D}${sbindir}
    install -m 0755 ${B}/h432b-power-inventory ${D}${sbindir}/
}

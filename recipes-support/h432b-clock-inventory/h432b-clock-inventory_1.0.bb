# SPDX-License-Identifier: MIT
SUMMARY = "Read-only H432B clock, DRAM and voltage-selector inventory"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"
SRC_URI = "file://clock-inventory.c"
S = "${UNPACKDIR}"
COMPATIBLE_MACHINE = "^h432b$"
do_compile() {
    ${CC} ${CFLAGS} -D_FILE_OFFSET_BITS=64 -std=c11 -Wall -Wextra -Werror \
        ${S}/clock-inventory.c ${LDFLAGS} -o ${B}/h432b-clock-inventory
}
do_install() {
    install -d ${D}${sbindir}
    install -m 0755 ${B}/h432b-clock-inventory ${D}${sbindir}/
}

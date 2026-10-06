# SPDX-License-Identifier: MIT
SUMMARY = "Read-only H432B reset and INFORM register inventory"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"
SRC_URI = "file://reboot-probe.c file://reboot-retention.c"
S = "${UNPACKDIR}"
COMPATIBLE_MACHINE = "^h432b$"
do_compile() {
    ${CC} ${CFLAGS} -std=c11 -Wall -Wextra -Werror ${LDFLAGS} \
        ${S}/reboot-probe.c -o ${B}/h432b-reboot-probe
    ${CC} ${CFLAGS} -std=c11 -Wall -Wextra -Werror ${LDFLAGS} \
        ${S}/reboot-retention.c -o ${B}/h432b-reboot-retention
}
do_install() {
    install -d ${D}${sbindir}
    install -m 0755 ${B}/h432b-reboot-probe ${B}/h432b-reboot-retention ${D}${sbindir}/
}

# SPDX-License-Identifier: MIT
SUMMARY = "Bounded GPIO keyboard and selector bring-up recorder"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"
SRC_URI = "file://input-recorder.c"
S = "${UNPACKDIR}"
COMPATIBLE_MACHINE = "^h432b$"
inherit deploy
do_compile() {
    ${BUILD_CC} ${BUILD_CFLAGS} ${BUILD_LDFLAGS} -std=c11 -Wall -Wextra -Werror \
        ${S}/input-recorder.c -o ${B}/test-input-recorder
    ${B}/test-input-recorder --self-test
    ${CC} ${CFLAGS} -std=c11 -Wall -Wextra -Werror ${LDFLAGS} \
        ${S}/input-recorder.c -o ${B}/h432b-input-recorder
}
do_install() {
    install -d ${D}${sbindir}
    install -m 0755 ${B}/h432b-input-recorder ${D}${sbindir}/
}
do_deploy() {
    install -d ${DEPLOYDIR}/input-recorder
    install -m 0755 ${B}/h432b-input-recorder ${DEPLOYDIR}/input-recorder/
}
addtask deploy after do_compile before do_build

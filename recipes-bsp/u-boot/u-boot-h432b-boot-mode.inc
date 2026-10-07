# SPDX-License-Identifier: MIT
require u-boot-h432b-nand_2012.10.bb
SRC_URI += "file://0005-nand-autoboot.patch"
SUMMARY = "H432B NAND autoboot and one-shot INFORM7 boot-mode support"
SRC_URI += "file://0008-reboot-mode-test.patch file://reboot/u2bootmode.c \
            file://reboot/bootmode.h file://reboot/test-bootmode.c"
do_configure:prepend() {
    install -m 0644 ${UNPACKDIR}/reboot/u2bootmode.c ${UNPACKDIR}/reboot/bootmode.h ${S}/board/hims/u2/
}
do_compile:prepend() {
    ${BUILD_CC} -std=c99 -Wall -Wextra -Werror \
        ${UNPACKDIR}/reboot/test-bootmode.c -o ${B}/test-bootmode
    ${B}/test-bootmode
}

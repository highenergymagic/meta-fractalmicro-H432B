# SPDX-License-Identifier: MIT
require u-boot-h432b-nand-auto_2012.10.bb
SUMMARY = "RAM-only experimental INFORM7 reboot-to-fastboot consumer"
SRC_URI += "file://0008-reboot-mode-test.patch file://reboot/u2bootmode.c \
            file://reboot/bootmode.h file://reboot/test-bootmode.c"
H432B_UBOOT_ROLE = "ram-reboot-test"
H432B_UBOOT_WARNING = "RAM ONLY; INFORM7 experiment, not a CE carrier. Reset retention and end-to-end handoff need qualification."
do_configure:prepend() {
    install -m 0644 ${UNPACKDIR}/reboot/u2bootmode.c ${UNPACKDIR}/reboot/bootmode.h ${S}/board/hims/u2/
}
do_compile:prepend() {
    ${BUILD_CC} -std=c99 -Wall -Wextra -Werror \
        ${UNPACKDIR}/reboot/test-bootmode.c -o ${B}/test-bootmode
    ${B}/test-bootmode
}

# SPDX-License-Identifier: MIT
require u-boot-h432b-bootstate_2012.10.bb
SUMMARY = "H432B persistent A/B slot selection with USB maintenance fallback"
SRC_URI += "file://bootstate/ab-autoboot.patch file://reboot/u2bootmode.c file://reboot/bootmode.h file://reboot/test-bootmode.c"
H432B_UBOOT_ROLE = "ram-ab"
H432B_UBOOT_WARNING = "High-RAM A/B stage. Requires provisioned bootstate and compatible slot images. Not directly flashable."
do_configure:prepend() {
    install -m 0644 ${UNPACKDIR}/reboot/u2bootmode.c ${UNPACKDIR}/reboot/bootmode.h ${S}/board/hims/u2/
}
do_compile:prepend() {
    ${BUILD_CC} -std=c99 -Wall -Wextra -Werror ${UNPACKDIR}/reboot/test-bootmode.c -o ${B}/test-bootmode
    ${B}/test-bootmode
}

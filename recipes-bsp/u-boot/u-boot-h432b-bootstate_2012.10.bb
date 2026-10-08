# SPDX-License-Identifier: MIT
require u-boot-h432b-nand-timer_2012.10.bb
SUMMARY = "H432B explicit redundant UBI bootstate qualification stage"
SRC_URI += "file://bootstate/bootstate.h file://bootstate/nand-write-boundary.h file://bootstate/test-bootstate.c \
            file://bootstate/u2bootstate.c file://bootstate/bootstate-ubi.patch \
            file://bootstate/bootstate-config.patch file://bootstate/bootstate-handoff.patch"
H432B_UBOOT_ROLE = "ram-bootstate"
H432B_UBOOT_WARNING = "RAM-only qualification stage. Explicit bootstate consumption writes the Linux UBI pool. No automatic slot activation or CE carrier."
do_configure:prepend() {
    install -m 0644 ${UNPACKDIR}/bootstate/bootstate.h ${UNPACKDIR}/bootstate/nand-write-boundary.h ${UNPACKDIR}/bootstate/u2bootstate.c ${S}/board/hims/u2/
}
do_compile:prepend() {
    ${BUILD_CC} -std=c99 -O2 -Wall -Wextra -Werror \
        ${UNPACKDIR}/bootstate/test-bootstate.c -o ${B}/test-bootstate
    ${B}/test-bootstate
}

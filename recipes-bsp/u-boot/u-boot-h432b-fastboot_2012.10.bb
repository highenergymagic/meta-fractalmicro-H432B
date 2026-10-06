# SPDX-License-Identifier: MIT
require u-boot-h432b.inc
SRC_URI += "file://0001-ram-loader-rev52.patch file://0003-fastboot-ram53.patch \
    file://fastboot/u2fastboot.c file://fastboot/u2fastboot.h \
    file://fastboot/test-fastboot.c"
H432B_UBOOT_ROLE = "ram53-fastboot-only"
H432B_UBOOT_ENTRY = "46000000"
H432B_UBOOT_WARNING = "RAM53 fastboot at 0x46000000. RAM ONLY. No NAND/CE update carrier. Flash and erase are unsupported."
do_configure:prepend() {
    install -m 0644 ${UNPACKDIR}/identity/identity.h ${UNPACKDIR}/identity/u2identity.c ${S}/board/hims/u2/
    install -m 0644 ${UNPACKDIR}/fastboot/u2fastboot.c ${S}/board/hims/u2/
    install -m 0644 ${UNPACKDIR}/fastboot/u2fastboot.h ${S}/board/hims/u2/
}
do_compile:prepend() {
    # Native parser tests run inside the same pinned container, no USB access.
    ${BUILD_CC} -std=c99 -Wall -Wextra -Werror \
        -I${S}/board/hims/u2 ${UNPACKDIR}/fastboot/test-fastboot.c \
        -o ${B}/test-fastboot
    ${B}/test-fastboot
    ${BUILD_CC} -std=c99 -Wall -Wextra -Werror \
        ${UNPACKDIR}/identity/test-identity.c -o ${B}/test-identity
    ${B}/test-identity
}

# Apply after subclass patches so every Linux-capable fastboot/NAND stage
# gets the same factory identity handoff without modifying the CE bootstrap.
SRC_URI:append = " file://0010-factory-identity.patch file://identity/identity.h file://identity/u2identity.c file://identity/test-identity.c"

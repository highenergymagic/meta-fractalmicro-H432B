# SPDX-License-Identifier: MIT
require u-boot-h432b-bootstate_2012.10.bb
SUMMARY = "H432B persistent A/B slot selection with USB maintenance fallback"
SRC_URI += "file://nand/check-bch-equivalence.py file://nand/test-bch-equivalence.c file://nand/nand-timing.h file://nand/u2nandtiming.c file://nand/crc-timing.patch file://0007-bch-subpage-read.patch file://bootstate/reuse-ubi-attach.patch file://bootstate/ab-autoboot.patch file://reboot/u2bootmode.c file://reboot/bootmode.h file://reboot/test-bootmode.c"
H432B_UBOOT_ROLE = "ram-ab"
H432B_UBOOT_WARNING = "High-RAM A/B stage. Requires provisioned bootstate and compatible slot images. Not directly flashable."
do_configure:append() {
    printf '\n#define CONFIG_BCH_CONST_PARAMS 1\n#define CONFIG_BCH_CONST_M 13\n#define CONFIG_BCH_CONST_T 8\n#define CONFIG_H432B_NAND_TIMING 1\n#define CONFIG_U2_NAND_SUBPAGE 1\n#define CONFIG_H432B_REUSE_UBI 1\n' >> ${S}/include/configs/hims_u2.h
}
do_configure:prepend() {
    install -m 0644 ${UNPACKDIR}/nand/nand-timing.h ${S}/include/
    install -m 0644 ${UNPACKDIR}/nand/u2nandtiming.c ${S}/board/hims/u2/
    install -m 0644 ${UNPACKDIR}/reboot/u2bootmode.c ${UNPACKDIR}/reboot/bootmode.h ${S}/board/hims/u2/
}
do_compile:prepend() {
    python3 ${UNPACKDIR}/nand/check-bch-equivalence.py --source ${S} --cc "${BUILD_CC}"
    ${BUILD_CC} -std=c99 -Wall -Wextra -Werror ${UNPACKDIR}/reboot/test-bootmode.c -o ${B}/test-bootmode
    ${B}/test-bootmode
}

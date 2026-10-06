# SPDX-License-Identifier: MIT
require u-boot-h432b-nand-profile_2012.10.bb
SUMMARY = "RAM-only NAND profiler with a PWM4 hardware clock"
SRC_URI += "file://timer/u2timer.c file://timer/timer-test-shim.h file://timer/test-timer.c"
H432B_UBOOT_ROLE = "ram-nand-timer"
H432B_UBOOT_WARNING = "RAM-only hardware-timer experiment. Not a NAND carrier; all NAND writes blocked."
do_configure:append() {
    install -m 0644 ${UNPACKDIR}/timer/u2timer.c ${S}/arch/arm/cpu/armv7/s5p-common/timer.c
}
do_compile:prepend() {
    ${BUILD_CC} -std=c99 -Wall -Wextra -Werror -Wno-unused-parameter \
        ${UNPACKDIR}/timer/test-timer.c -o ${B}/test-timer
    ${B}/test-timer
}

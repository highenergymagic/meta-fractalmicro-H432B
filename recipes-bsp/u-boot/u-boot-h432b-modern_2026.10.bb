# SPDX-License-Identifier: MIT
SUMMARY = "H432B modern U-Boot RAM-entry fastboot transport"
LICENSE = "GPL-2.0-or-later"
LIC_FILES_CHKSUM = "file://Licenses/README;md5=2ca5f2c35c8cc335f0a19756634782f1"
FILESEXTRAPATHS:prepend := "${THISDIR}/files/bootstate:${THISDIR}/files/fastboot:${THISDIR}/files/identity:${THISDIR}/files/reboot:"
SRC_URI = "git://github.com/u-boot/u-boot.git;protocol=https;nobranch=1 \
           file://0001-h432b-modern-foundation.patch \
           file://0002-h432b-modern-usb.patch \
           file://0003-h432b-modern-ram-layout.patch \
           file://0004-h432b-modern-nand-reader.patch \
           file://0005-h432b-modern-kernel-verification.patch \
           file://0006-h432b-modern-volume-flash.patch \
           file://0007-h432b-modern-nand-write-callback.patch \
           file://0008-h432b-modern-slot-activation.patch \
           file://0009-h432b-modern-getvar.patch \
           file://0010-h432b-modern-linux-handoff.patch \
           file://0011-h432b-modern-autoboot.patch \
           file://0012-h432b-modern-read-performance.patch \
           file://0013-h432b-modern-cached-boot.patch \
           file://0014-h432b-modern-device-mode-reset.patch \
           file://0015-dwc2-reset-caller-role.patch \
           file://0016-h432b-cache-qualification-switch.patch \
           file://0017-h432b-cache-handoff-audit.patch \
           file://0018-h432b-internal-sd.patch \
           file://0019-h432b-recovery-menu.patch \
           file://0020-h432b-systemext-fastboot.patch \
           file://0021-h432b-sparse-fastboot.patch \
           file://modern/check-bch-equivalence.py file://nand/test-bch-equivalence.c \
           file://bootmode.h file://modern/u2_braille.h \
           file://identity.h file://usb-identity.h \
           file://bootstate.h file://u2fastboot.h \
           file://fastboot-slots.h file://fastboot-targets.h \
           file://nand-write-boundary.h file://slot-pair.h file://sparse-image.h"
SRCREV = "5508406582f6f7120dce0c4b3059819a41788db2"
BB_GIT_SHALLOW = "1"
BB_GIT_SHALLOW_DEPTH = "1"
B = "${WORKDIR}/build"
inherit deploy h432b-build-identity
COMPATIBLE_MACHINE = "^h432b$"
PACKAGE_ARCH = "${MACHINE_ARCH}"
DEPENDS = "libgcc bison-native flex-native dtc-native"
PACKAGES = ""
SOURCE_DATE_EPOCH = "1791158400"
H432B_MODERN_ENTRY = "42000000"
H432B_MODERN_COMMAND = "h432bstorage; fastboot usb 0; h432bboot; fastboot usb 0"
H432B_MODERN_DIR = "modern-ram-fastboot"
EXTRA_OEMAKE = 'O=${B} CROSS_COMPILE=${TARGET_PREFIX} SOURCE_DATE_EPOCH=${SOURCE_DATE_EPOCH}'
EXTRA_OEMAKE += 'CC="${TARGET_PREFIX}gcc ${TOOLCHAIN_OPTIONS} ${DEBUG_PREFIX_MAP}"'
EXTRA_OEMAKE += 'HOSTCC="${BUILD_CC} ${BUILD_CFLAGS} ${BUILD_LDFLAGS}"'
EXTRA_OEMAKE += 'HOSTCXX="${BUILD_CXX} ${BUILD_CXXFLAGS} ${BUILD_LDFLAGS}"'

do_configure() {
    unset CFLAGS CPPFLAGS LDFLAGS
    install -m 0644 ${UNPACKDIR}/bootstate.h ${S}/board/fractalmicro/h432b/
    install -m 0644 ${UNPACKDIR}/identity.h ${UNPACKDIR}/usb-identity.h ${S}/board/fractalmicro/h432b/
    install -m 0644 ${UNPACKDIR}/bootmode.h ${UNPACKDIR}/modern/u2_braille.h ${S}/board/fractalmicro/h432b/
    install -m 0644 ${UNPACKDIR}/u2fastboot.h ${S}/board/fractalmicro/h432b/
    install -m 0644 ${UNPACKDIR}/fastboot-slots.h ${UNPACKDIR}/fastboot-targets.h \
        ${UNPACKDIR}/slot-pair.h ${UNPACKDIR}/sparse-image.h \
        ${UNPACKDIR}/nand-write-boundary.h ${S}/board/fractalmicro/h432b/
    oe_runmake -C ${S} h432b_defconfig
    ${S}/scripts/config --file ${B}/.config --set-val TEXT_BASE 0x${H432B_MODERN_ENTRY} \
        --set-str BOOTCOMMAND "${H432B_MODERN_COMMAND}"
    oe_runmake -C ${S} olddefconfig
    for feature in MTD_RAW_NAND SYS_NAND_SELF_INIT NAND_ECC_BCH BCH CMD_UBI FASTBOOT_FLASH FASTBOOT_FLASH_BOARD MMC_WRITE EFI_PARTITION; do
        grep -qx "CONFIG_$feature=y" ${B}/.config ||
            bbfatal "Missing required modern NAND feature: $feature"
    done
}
do_compile() {
    unset CFLAGS CPPFLAGS LDFLAGS
    python3 ${UNPACKDIR}/modern/check-bch-equivalence.py --source ${S} --cc "${BUILD_CC}"
    oe_runmake -C ${S} u-boot.bin
    ${TARGET_PREFIX}nm -n ${B}/u-boot | grep -qx "${H432B_MODERN_ENTRY} T _start" ||
        bbfatal "Incorrect modern RAM-entry load address"
}
do_install[noexec] = "1"
do_deploy() {
    install -d ${DEPLOYDIR}/${H432B_MODERN_DIR}
    install -m 0644 ${B}/u-boot.bin ${DEPLOYDIR}/${H432B_MODERN_DIR}/
    install -m 0644 ${B}/u-boot ${DEPLOYDIR}/${H432B_MODERN_DIR}/u-boot.elf
    install -m 0644 ${B}/u-boot.map ${DEPLOYDIR}/${H432B_MODERN_DIR}/
    printf '%s\n' 'Raw component: use only the role-specific deployment instructions; never flash u-boot.bin through EBOOT.' > ${DEPLOYDIR}/${H432B_MODERN_DIR}/ROLE.txt
}
addtask deploy after do_compile before do_build

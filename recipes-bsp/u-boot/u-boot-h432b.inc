# SPDX-License-Identifier: MIT
SUMMARY = "Historical H432B U-Boot port, raw artifact only"
LICENSE = "GPL-2.0-or-later"
LIC_FILES_CHKSUM = "file://COPYING;md5=1707d6db1d42237583f50183a5651ecb"
SRC_URI = "git://github.com/u-boot/u-boot.git;protocol=https;nobranch=1 \
    file://0001-hims-u2-rev51.patch file://0002-reproducible-timestamp.patch"
SRCREV = "6528ff0109d81c1f21d20f9f1370782bccf87bcb"
BB_GIT_SHALLOW = "1"
BB_GIT_SHALLOW_DEPTH = "1"
B = "${S}"
inherit deploy h432b-arm-gnu
COMPATIBLE_MACHINE = "^h432b$"
PACKAGE_ARCH = "${MACHINE_ARCH}"
INHIBIT_DEFAULT_DEPS = "1"
INHIBIT_PACKAGE_STRIP = "1"
INHIBIT_PACKAGE_DEBUG_SPLIT = "1"
PACKAGES = ""
SOURCE_DATE_EPOCH = "1791158400"
EXTRA_OEMAKE = "CROSS_COMPILE=${H432B_CROSS} SUBDIR_TOOLS= SOURCE_DATE_EPOCH=${SOURCE_DATE_EPOCH}"
do_configure() {
    unset CFLAGS CPPFLAGS LDFLAGS
    oe_runmake hims_u2_config
}
do_compile() {
    unset CFLAGS CPPFLAGS LDFLAGS
    oe_runmake u-boot.bin
    ${H432B_CROSS}nm -n ${S}/u-boot | grep -qx "${H432B_UBOOT_ENTRY} T _start" ||
        bbfatal "Wrong link address: refuse to deploy this artifact"
}
do_install[noexec] = "1"
do_deploy() {
    install -d ${DEPLOYDIR}/${H432B_UBOOT_ROLE}
    install -m 0644 ${S}/u-boot.bin ${DEPLOYDIR}/${H432B_UBOOT_ROLE}/u-boot.bin
    install -m 0644 ${S}/u-boot ${DEPLOYDIR}/${H432B_UBOOT_ROLE}/u-boot.elf
    install -m 0644 ${S}/u-boot.map ${DEPLOYDIR}/${H432B_UBOOT_ROLE}/u-boot.map
    printf '%s\n' "${H432B_UBOOT_WARNING}" > ${DEPLOYDIR}/${H432B_UBOOT_ROLE}/ROLE.txt
}
addtask deploy after do_compile before do_build

# SPDX-License-Identifier: MIT
SUMMARY = "Historical H432B U-Boot port, raw artifact only"
LICENSE = "GPL-2.0-or-later"
LIC_FILES_CHKSUM = "file://COPYING;md5=1707d6db1d42237583f50183a5651ecb"
SRC_URI = "git://github.com/u-boot/u-boot.git;protocol=https;nobranch=1 \
    file://0001-hims-u2-rev51.patch file://0002-reproducible-timestamp.patch file://0007-gcc15-compatibility.patch"
SRCREV = "6528ff0109d81c1f21d20f9f1370782bccf87bcb"
BB_GIT_SHALLOW = "1"
BB_GIT_SHALLOW_DEPTH = "1"
B = "${S}"
inherit deploy h432b-build-identity
COMPATIBLE_MACHINE = "^h432b$"
PACKAGE_ARCH = "${MACHINE_ARCH}"
DEPENDS += "libgcc"
INHIBIT_PACKAGE_STRIP = "1"
INHIBIT_PACKAGE_DEBUG_SPLIT = "1"
PACKAGES = ""
SOURCE_DATE_EPOCH = "1791158400"
# This historical source predates C23. Preserve its GNU89 language contract.
EXTRA_OEMAKE = 'CROSS_COMPILE=${TARGET_PREFIX} SUBDIR_TOOLS= SOURCE_DATE_EPOCH=${SOURCE_DATE_EPOCH}'
EXTRA_OEMAKE += 'CC="${TARGET_PREFIX}gcc ${TOOLCHAIN_OPTIONS} ${DEBUG_PREFIX_MAP} -std=gnu89"'
EXTRA_OEMAKE += 'HOSTCC="${BUILD_CC} ${BUILD_CFLAGS} ${BUILD_LDFLAGS}"'
do_configure() {
    unset CFLAGS CPPFLAGS LDFLAGS
    # B=S: discard objects and dependency paths from an earlier toolchain.
    # tidy retains the second-stage .bin installed by the chain recipe.
    oe_runmake tidy
    oe_runmake hims_u2_config
}
do_compile() {
    unset CFLAGS CPPFLAGS LDFLAGS
    oe_runmake u-boot.bin
    ${TARGET_PREFIX}nm -n ${S}/u-boot | grep -qx "${H432B_UBOOT_ENTRY} T _start" ||
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

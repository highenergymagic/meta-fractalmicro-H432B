# SPDX-License-Identifier: MIT
SUMMARY = "H432B internal SD controller identity"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"
inherit allarch
do_install() {
    install -d ${D}${nonarch_libdir}/openh432
    printf 'eb100000.mmc\n' > ${D}${nonarch_libdir}/openh432/storage-controller
}
FILES:${PN} = "${nonarch_libdir}/openh432/storage-controller"

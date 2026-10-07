# SPDX-License-Identifier: MIT
require linux-h432b-resume-test_6.12.111.bb
SUMMARY = "Opt-in internal SD write qualification kernel; NAND read-only"
KERNEL_PACKAGE_NAME = "kernel-sd-rw-test"
KERNEL_DEVICETREE = "samsung/s5pv210-hims-u2-sd-rw-test.dtb"
SRC_URI += "file://s5pv210-hims-u2-sd-rw-test.dts file://u2-sd-rw-test.config"
python __anonymous() {
    if d.getVar("H432B_NAND_PROFILE") != "readonly":
        bb.fatal("Internal SD test requires read-only NAND")
}
do_configure:append() {
    install -m 0644 ${UNPACKDIR}/s5pv210-hims-u2-sd-rw-test.dts ${S}/arch/arm/boot/dts/samsung/
    KCONFIG_CONFIG=${B}/.config ${S}/scripts/kconfig/merge_config.sh -m -O ${B} \
        ${B}/.config ${UNPACKDIR}/u2-sd-rw-test.config
    oe_runmake -C ${S} O=${B} olddefconfig
    for option in FAT_FS VFAT_FS NLS_CODEPAGE_437 NLS_ISO8859_1; do
        grep -qx "CONFIG_$option=y" ${B}/.config || bbfatal "Missing SD filesystem option: $option"
    done
}

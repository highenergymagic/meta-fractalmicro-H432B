# SPDX-License-Identifier: MIT
require linux-h432b-resume-test_6.12.111.bb
require h432b-external-sd.inc
SUMMARY = "Opt-in read-only external SD slot qualification kernel"
KERNEL_PACKAGE_NAME = "kernel-external-sd-test"
KERNEL_DEVICETREE = "samsung/s5pv210-hims-u2-external-sd-test.dtb"
SRC_URI += "file://s5pv210-hims-u2-external-sd-test.dts"
python __anonymous() {
    if d.getVar("H432B_NAND_PROFILE") != "readonly":
        bb.fatal("External SD test requires read-only NAND")
}
do_configure:append() {
    install -m 0644 ${UNPACKDIR}/s5pv210-hims-u2-external-sd-test.dts ${S}/arch/arm/boot/dts/samsung/
}

# SPDX-License-Identifier: MIT
require linux-h432b_6.12.111.bb
SUMMARY = "Opt-in H432B warm reboot-mode kernel experiment"
# Isolate shared source, packaging and deploy products from the normal kernel.
PROVIDES:remove = "virtual/kernel"
KERNEL_PACKAGE_NAME = "kernel-reboot-test"
KERNEL_DEVICETREE = "samsung/s5pv210-hims-u2-reboot-test.dtb"
SRC_URI += "file://s5pv210-hims-u2-reboot-test.dts file://u2-reboot-test.config"

do_configure:append() {
    install -m 0644 ${UNPACKDIR}/s5pv210-hims-u2-reboot-test.dts ${S}/arch/arm/boot/dts/samsung/
    KCONFIG_CONFIG=${B}/.config ${S}/scripts/kconfig/merge_config.sh -m -O ${B} \
        ${B}/.config ${UNPACKDIR}/u2-reboot-test.config
    oe_runmake -C ${S} O=${B} olddefconfig
    for option in REBOOT_MODE SYSCON_REBOOT_MODE; do
        grep -qx "CONFIG_$option=y" ${B}/.config ||
            bbfatal "Missing experimental reboot option: $option"
    done
}

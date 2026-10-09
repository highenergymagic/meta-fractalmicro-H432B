# SPDX-License-Identifier: MIT
require linux-h432b-runtime_6.12.111.bb
SUMMARY = "H432B explicit RTL8712 SDIO transport qualification"
H432B_KERNEL_PROVIDER_REMOVE = "virtual/kernel"
KERNEL_PACKAGE_NAME = "kernel-wifi-test"
KERNEL_DEPLOYSUBDIR = "kernel-wifi-test"
SRC_URI += "file://h432b-wifi-debug.config"
do_configure:append() {
    KCONFIG_CONFIG=${B}/.config ${S}/scripts/kconfig/merge_config.sh -m -O ${B} \
        ${B}/.config ${UNPACKDIR}/h432b-wifi-debug.config
    oe_runmake -C ${S} O=${B} olddefconfig
    grep -qx 'CONFIG_H432B_WIFI_DIAGNOSTICS=y' ${B}/.config || bbfatal 'Wi-Fi diagnostics missing'
}

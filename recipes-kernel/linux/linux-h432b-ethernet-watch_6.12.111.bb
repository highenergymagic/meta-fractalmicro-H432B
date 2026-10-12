# SPDX-License-Identifier: MIT
require linux-h432b-runtime_6.12.111.bb
SUMMARY = "Opt-in integrated H432B kernel with lockup and function tracing diagnostics"
H432B_KERNEL_PROVIDER_REMOVE = "virtual/kernel"
KERNEL_PACKAGE_NAME = "kernel-ethernet-watch"
KERNEL_DEPLOYSUBDIR = "kernel-ethernet-watch"
SRC_URI += "file://h432b-ethernet-watch.config"

do_configure:append() {
    KCONFIG_CONFIG=${B}/.config ${S}/scripts/kconfig/merge_config.sh -m -O ${B} \
        ${B}/.config ${UNPACKDIR}/h432b-ethernet-watch.config
    oe_runmake -C ${S} O=${B} olddefconfig
    for option in SOFTLOCKUP_DETECTOR DETECT_HUNG_TASK MAGIC_SYSRQ FUNCTION_TRACER FUNCTION_GRAPH_TRACER; do
        grep -qx "CONFIG_$option=y" ${B}/.config || bbfatal "Missing diagnostic option: $option"
    done
}

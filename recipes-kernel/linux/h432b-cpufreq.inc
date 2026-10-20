# SPDX-License-Identifier: MIT
SRC_URI += "file://0041-s5pv210-fixed-bus-cpufreq.patch \
            file://s5pv210-fixed-cpufreq.c file://h432b-cpufreq.config"

do_configure:append() {
    install -m 0644 ${UNPACKDIR}/s5pv210-fixed-cpufreq.c ${S}/drivers/cpufreq/
    KCONFIG_CONFIG=${B}/.config ${S}/scripts/kconfig/merge_config.sh -m -O ${B} \
        ${B}/.config ${UNPACKDIR}/h432b-cpufreq.config
    oe_runmake -C ${S} O=${B} olddefconfig
    for option in CPU_FREQ ARM_S5PV210_FIXED_CPUFREQ CPU_FREQ_DEFAULT_GOV_PERFORMANCE CPU_FREQ_GOV_USERSPACE CPU_FREQ_GOV_ONDEMAND; do
        grep -qx "CONFIG_$option=y" ${B}/.config ||
            bbfatal "Missing frequency-scaling option: $option"
    done
    if grep -qx 'CONFIG_ARM_S5PV210_CPUFREQ=y' ${B}/.config; then
        bbfatal "Conflicting S5PV210 CPUFreq drivers"
    fi
}

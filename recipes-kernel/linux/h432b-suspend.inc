# SPDX-License-Identifier: MIT
# Merge last: peripheral diagnostic fragments may explicitly disable suspend.
SRC_URI += "file://0009-factory-resume-test.patch file://0021-h432b-suspend-peripheral-retention.patch file://0022-smsc911x-suspend-irq-order.patch file://0023-smsc911x-bound-loopback-rx.patch file://0024-h432b-ethernet-full-resume.patch file://0025-h432b-rtc-no-alarm-wake.patch file://h432b-rtc.config file://s5pv210-hims-u2-suspend.dtsi file://u2-resume-test.config"
do_configure:append() {
    install -m 0644 ${UNPACKDIR}/s5pv210-hims-u2-suspend.dtsi ${S}/arch/arm/boot/dts/samsung/
    KCONFIG_CONFIG=${B}/.config ${S}/scripts/kconfig/merge_config.sh -m -O ${B} \
        ${B}/.config ${UNPACKDIR}/u2-resume-test.config ${UNPACKDIR}/h432b-rtc.config
    oe_runmake -C ${S} O=${B} olddefconfig
    for option in SUSPEND PM_SLEEP_DEBUG ARM_PATCH_PHYS_VIRT AUTO_ZRELADDR RTC_CLASS RTC_DRV_S3C RTC_HCTOSYS; do
        grep -qx "CONFIG_$option=y" ${B}/.config || bbfatal "Missing suspend option: $option"
    done
    if grep -Eq '^CONFIG_PM_TEST_SUSPEND=(y|m)$' ${B}/.config; then
        bbfatal "Automatic kernel test suspend is forbidden"
    fi
}

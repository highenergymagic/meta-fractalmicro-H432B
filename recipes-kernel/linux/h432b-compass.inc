# SPDX-License-Identifier: MIT
SRC_URI += "file://0026-ami603-iio.patch file://0026a-ami603-binding.patch file://ami603.c file://s5pv210-hims-u2-compass.dtsi file://h432b-compass.config"
do_configure:append() {
    install -m 0644 ${UNPACKDIR}/ami603.c ${S}/drivers/iio/magnetometer/
    install -m 0644 ${UNPACKDIR}/s5pv210-hims-u2-compass.dtsi ${S}/arch/arm/boot/dts/samsung/
    KCONFIG_CONFIG=${B}/.config ${S}/scripts/kconfig/merge_config.sh -m -O ${B} \
        ${B}/.config ${UNPACKDIR}/h432b-compass.config
    oe_runmake -C ${S} O=${B} olddefconfig
    for option in IIO AMI603 I2C_GPIO REGULATOR_FIXED_VOLTAGE; do
        grep -qx "CONFIG_$option=y" ${B}/.config || bbfatal "Missing compass option: $option"
    done
}

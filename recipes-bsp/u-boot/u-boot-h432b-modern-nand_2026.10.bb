# SPDX-License-Identifier: MIT
require u-boot-h432b-modern_2026.10.bb
SUMMARY = "H432B modern U-Boot factory-compatible NAND carrier"
SRC_URI += "file://chain/ce-carrier.py"
H432B_MODERN_ENTRY = "40021000"
H432B_MODERN_COMMAND = "h432bauto; fastboot usb 0; h432bboot; fastboot usb 0"
H432B_MODERN_DIR = "modern-nand-ce-carrier"

do_configure:append() {
    ${S}/scripts/config --file ${B}/.config --enable H432B_NAND_BOOT_DCACHE
    oe_runmake -C ${S} olddefconfig
    grep -qx 'CONFIG_H432B_NAND_BOOT_DCACHE=y' ${B}/.config || bbfatal "NAND boot requires caching"
}

do_compile:append() {
    python3 ${UNPACKDIR}/chain/ce-carrier.py ${B}/u-boot.bin \
        ${B}/u-boot-ce.b000ff --modern-elf ${B}/u-boot
}

do_deploy:append() {
    install -m 0644 ${B}/u-boot-ce.b000ff ${DEPLOYDIR}/${H432B_MODERN_DIR}/
    printf '%s\n' 'Factory NK-slot CE carrier, physical entry 0x40021000. Preserves EBOOT. Requires qualified Linux UBI slots. Hardware qualification is separate from build success.' > ${DEPLOYDIR}/${H432B_MODERN_DIR}/ROLE.txt
}

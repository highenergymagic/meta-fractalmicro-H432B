# SPDX-License-Identifier: MIT
require u-boot-h432b-fastboot_2012.10.bb
SUMMARY = "H432B RAM loader with read-only BCH/UBI NAND boot"
SRC_URI += "file://0004-nand-ubi-readonly.patch file://nand/u2nand.c file://nand/check-memory.py"
H432B_UBOOT_ROLE = "ram54-nand-reader"
H432B_UBOOT_WARNING = "RAM54 NAND reader at 0x46000000. RAM ONLY; not a CE carrier. All NAND writes blocked."
do_configure:prepend() {
    install -m 0644 ${UNPACKDIR}/nand/u2nand.c ${S}/board/hims/u2/
}

do_compile:append() {
    python3 ${UNPACKDIR}/nand/check-memory.py ${S}/u-boot.map
}

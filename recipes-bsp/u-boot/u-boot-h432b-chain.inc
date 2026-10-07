# SPDX-License-Identifier: MIT
require u-boot-h432b.inc
SUMMARY = "H432B CE-address NAND bootstrap with verified high-RAM second stage"
SRC_URI += "file://0006-nand-chain.patch file://chain/u2chain.c \
            file://chain/u2stage.S file://chain/stage-header.py file://chain/ce-carrier.py file://nand/check-memory.py"
H432B_UBOOT_ENTRY = "40021000"
# The concrete carrier recipe supplies its stage, output paths and role.
do_configure[depends] += "${H432B_CHAIN_STAGE_RECIPE}:do_deploy"
do_configure:prepend() {
    install -m 0644 ${UNPACKDIR}/chain/u2chain.c ${S}/board/hims/u2/
    install -m 0644 ${UNPACKDIR}/chain/u2stage.S ${S}/board/hims/u2/
    install -m 0644 ${DEPLOY_DIR_IMAGE}/${H432B_CHAIN_STAGE_DIR}/u-boot.bin ${S}/board/hims/u2/u2stage.bin
    python3 ${UNPACKDIR}/chain/stage-header.py \
        ${S}/board/hims/u2/u2stage.bin ${S}/board/hims/u2/u2stage-meta.h
}

do_compile:append() {
    python3 ${UNPACKDIR}/nand/check-memory.py ${S}/u-boot.map --chain
    python3 ${UNPACKDIR}/chain/ce-carrier.py ${S}/u-boot.bin ${B}/u-boot-ce.b000ff --chain
}
do_deploy:append() {
    install -d ${DEPLOYDIR}/${H432B_CHAIN_CARRIER_DIR}
    install -m 0644 ${B}/u-boot-ce.b000ff ${DEPLOYDIR}/${H432B_CHAIN_CARRIER_DIR}/u-boot-ce.b000ff
    printf '%s\n' 'CE carrier at 0x80020000 for the factory NK image slot; contains the configured verified RAM stage. Build is not hardware qualification.' > ${DEPLOYDIR}/${H432B_CHAIN_CARRIER_DIR}/ROLE.txt
}

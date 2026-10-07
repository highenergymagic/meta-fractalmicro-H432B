# SPDX-License-Identifier: MIT
SUMMARY = "H432B bounded NAND qualification kernel: CIP 6.12 plus separate RT patch"
LICENSE = "GPL-2.0-only"
LIC_FILES_CHKSUM = "file://COPYING;md5=6bc538ed5bd9a7fc9398086aedcd7e46"
LINUX_VERSION = "6.12.111"
SRC_URI = "https://www.kernel.org/pub/linux/kernel/projects/cip/6.12/linux-cip-6.12.111-cip32.tar.xz;name=kernel \
    https://www.kernel.org/pub/linux/kernel/projects/rt/6.12/patch-6.12.111-rt21.patch.xz;name=rt \
    file://0001-readonly-storage-integration.patch \
    file://0002-nand-reader.patch \
    file://0003-usb-host-diagnostics.patch \
    file://0004-usb-host-required-power-bit.patch \
    file://0005-sdio-cis-end.patch \
    file://0006-audio-clock-codec.patch \
    file://0007-hims-u2-audio.patch \
    file://0008-nand-bch-window.patch file://0015-nand-word-reads.patch file://test-nand-guard.c file://nand-profile.py \
    file://s5pv210-hims-u2.dts \
    file://u2-ram.config file://u2-storage.config file://u2-sdio.config \
    file://u2-audio.config file://u2-systemd.config file://u2-input.config file://u2-ethernet.config \
"
SRC_URI[kernel.sha256sum] = "879b45f18c1bd8322b2026f72642faedcb77242418cb10a5aa56f5404f647059"
SRC_URI[rt.sha256sum] = "8b9b7b59f0372e1e70b5fd99729765d335928ef0a0196fb1e869afc45fc69b55"
inherit kernel h432b-build-identity
# Keep the legacy RAM kernel buildable beside the normal runtime provider.
H432B_KERNEL_PROVIDER_REMOVE ?= "virtual/kernel"
PROVIDES:remove = "${H432B_KERNEL_PROVIDER_REMOVE}"
KERNEL_PACKAGE_NAME = "kernel-legacy"
# Preserve legacy bundle paths; derived diagnostic kernels remain isolated.
KERNEL_DEPLOYSUBDIR = "${@'' if d.getVar('PN') == 'linux-h432b' else d.getVar('KERNEL_PACKAGE_NAME')}"
# Override kernel.bbclass after inheritance so do_symlink_kernsrc moves the
# actual unpacked tree into the shared kernel source before patching.
S = "${UNPACKDIR}/linux-cip-6.12.111-cip32"
COMPATIBLE_MACHINE = "^h432b$"
DEPENDS += "flex-native openssl-native"
KERNEL_LOCALVERSION = ""
SOURCE_DATE_EPOCH = "1791158400"
IMAGE_VERSION_SUFFIX = "-${SOURCE_DATE_EPOCH}"

H432B_NAND_PROFILE ?= "readonly"

do_configure:prepend() {
    install -m 0644 ${UNPACKDIR}/s5pv210-hims-u2.dts ${S}/arch/arm/boot/dts/samsung/
    python3 ${UNPACKDIR}/nand-profile.py --profile ${H432B_NAND_PROFILE} \
        ${S}/arch/arm/boot/dts/samsung/s5pv210-hims-u2.dts
    oe_runmake -C ${S} O=${B} allnoconfig
    KCONFIG_CONFIG=${B}/.config ${S}/scripts/kconfig/merge_config.sh -m -O ${B} \
        ${B}/.config ${UNPACKDIR}/u2-ram.config ${UNPACKDIR}/u2-storage.config \
        ${UNPACKDIR}/u2-sdio.config ${UNPACKDIR}/u2-audio.config \
        ${UNPACKDIR}/u2-systemd.config ${UNPACKDIR}/u2-input.config ${UNPACKDIR}/u2-ethernet.config
}
do_configure:append() {
    for option in PREEMPT_RT RD_XZ MTD_NAND_HIMS_U2 MTD_NAND_ECC_SW_BCH MTD_UBI_BLOCK SQUASHFS SQUASHFS_XZ UBIFS_FS MMC_SDHCI_S3C USB_G_SERIAL \
                  INPUT_EVDEV KEYBOARD_GPIO SMSC911X SMSC_PHY SND_SOC_HIMS_U2 CGROUPS MEMCG CGROUP_PIDS SECCOMP_FILTER FHANDLE; do
        grep -qx "CONFIG_$option=y" ${B}/.config ||
            bbfatal "Missing required board/guard option: $option"
    done
    grep -qx '# CONFIG_MODULES is not set' ${B}/.config ||
        bbfatal "This baseline has no module-installation contract yet"
}

# kernel.org release patches do not carry OE's local provenance header.
# Fetch verifies the upstream archive checksum first; add metadata only to
# its unpacked copy, preserving every byte of the actual patch payload.
python do_patch:prepend() {
    from pathlib import Path
    patch = Path(d.getVar("UNPACKDIR")) / "patch-6.12.111-rt21.patch"
    header = (
        "Subject: Apply upstream PREEMPT_RT 6.12.111-rt21 release\n"
        "Upstream-Status: Inappropriate [upstream maintained RT release patch]\n"
        "Source: https://www.kernel.org/pub/linux/kernel/projects/rt/6.12/patch-6.12.111-rt21.patch.xz\n\n"
    ).encode()
    body = patch.read_bytes()
    if not body.startswith(header):
        patch.write_bytes(header + body)
}

do_compile:prepend() {
    ${BUILD_CC} -std=c99 -Wall -Wextra -Werror \
        -I${S}/drivers/mtd/nand/raw ${UNPACKDIR}/test-nand-guard.c \
        -o ${B}/test-nand-guard
    ${B}/test-nand-guard
}

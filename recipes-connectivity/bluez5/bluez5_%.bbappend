# SPDX-License-Identifier: MIT
FILESEXTRAPATHS:prepend := "${THISDIR}/files:"
SRC_URI += "file://0001-h432b-bcsp-baud.patch file://hciattach_h432b.c"
do_configure:prepend() {
    install -m 0644 ${UNPACKDIR}/hciattach_h432b.c ${S}/tools/
}

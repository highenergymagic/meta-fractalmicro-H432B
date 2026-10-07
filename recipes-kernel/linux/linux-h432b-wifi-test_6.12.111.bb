# SPDX-License-Identifier: MIT
require linux-h432b-runtime_6.12.111.bb
SUMMARY = "H432B explicit RTL8712 SDIO transport qualification"
H432B_KERNEL_PROVIDER_REMOVE = "virtual/kernel"
KERNEL_PACKAGE_NAME = "kernel-wifi-test"
KERNEL_DEPLOYSUBDIR = "kernel-wifi-test"
SRC_URI += "file://0016-wifi-transport.patch file://h432b-wifi-transport.c file://h432b-wifi-power.h file://h432b-wifi-firmware.h file://h432b-wifi-irq.h file://h432b-wifi-events.h file://h432b-wifi-command.h"
do_configure:append() {
    install -m 0644 ${UNPACKDIR}/h432b-wifi-transport.c ${UNPACKDIR}/h432b-wifi-power.h ${UNPACKDIR}/h432b-wifi-firmware.h ${UNPACKDIR}/h432b-wifi-irq.h ${UNPACKDIR}/h432b-wifi-events.h ${UNPACKDIR}/h432b-wifi-command.h ${S}/drivers/mmc/core/
}

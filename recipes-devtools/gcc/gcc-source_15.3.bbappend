# SPDX-License-Identifier: MIT
# Shared source also feeds cross, SDK and runtime compiler recipes.
FILESEXTRAPATHS:prepend := "${THISDIR}/files:"
SRC_URI += "file://0001-arm-sequence-shift-scratch-registers.patch"

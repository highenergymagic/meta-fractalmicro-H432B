/* SPDX-License-Identifier: GPL-2.0-or-later */
.section .rodata.u2stage, "a", %progbits
.balign 4
.global u2_stage_start
u2_stage_start:
.incbin "u2stage.bin"
.global u2_stage_end
u2_stage_end:

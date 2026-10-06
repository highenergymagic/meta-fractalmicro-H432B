/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Opt-in RAM test loader only until reset qualification. */
#include <common.h>
#include <command.h>
#include <asm/io.h>
#include "bootmode.h"
#define U2_INFORM7 0xe010f01cUL
static unsigned mode_read(void *context) { return readl(context); }
static void mode_write(void *context, unsigned value)
{
    writel(value, context);
    __asm__ volatile("dsb" : : : "memory");
}
static int do_u2bootmode(cmd_tbl_t *cmdtp, int flag, int argc, char *const argv[])
{
    int result;
    if (argc != 1) return CMD_RET_USAGE;
    result = h432b_take_bootmode(mode_read, mode_write, (void *)U2_INFORM7);
    if (result < 0)
        puts("Boot request clear failed; remain in USB maintenance\n");
    else if (result)
        puts("Consumed one-shot fastboot request\n");
    /* Nonzero short-circuits NAND loading and falls into u2 probe. */
    return result ? CMD_RET_FAILURE : CMD_RET_SUCCESS;
}
U_BOOT_CMD(u2bootmode, 1, 0, do_u2bootmode,
           "consume the experimental one-shot boot request", "");

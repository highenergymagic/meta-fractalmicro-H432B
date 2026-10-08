/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <common.h>
#include <command.h>
#include <libfdt.h>
#define H432B_BOOTSTATE_UBOOT 1
#include "bootstate.h"
#ifdef CONFIG_H432B_NAND_TIMING
#include <nand-timing.h>
#endif
extern int h432b_ubi_bootstate_read(unsigned, void *);
extern int h432b_ubi_bootstate_replace(unsigned, const void *);
static int boot_slot = -1;
static unsigned boot_serial;
static unsigned long attach_ms, kernel_started, boot_started;
/* Called after image CRC checks and before bootz. RAM/manual boots carry no
 * successful-attempt claim. Preserve the image's normal kernel arguments. */
int u2_bootstate_handoff(void *fdt)
{
    const char *args;
    char commandline[2048];
    int node, len, count;
    if (boot_slot < 0) return 0;
    node = fdt_path_offset(fdt, "/chosen");
    if (node < 0) return -1;
    args = fdt_getprop(fdt, node, "bootargs", &len);
    if (!args || len < 1 || args[len - 1] ||
        strstr(args, "rauc.slot=") || strstr(args, "openh432.attempt="))
        return -1;
    count = snprintf(commandline, sizeof(commandline),
                     "%s rauc.slot=%c openh432.attempt=%u"
                     " openh432.loader_attach_ms=%lu"
                     " openh432.loader_kernel_ms=%lu"
                     " openh432.loader_total_ms=%lu",
                     args, 'A' + boot_slot, boot_serial, attach_ms,
                     get_timer(kernel_started), get_timer(boot_started));
    if (count < 0 || count >= sizeof(commandline)) return -1;
#ifdef CONFIG_H432B_NAND_TIMING
    if (h432b_nand_timing_format(commandline + count,
                                sizeof(commandline) - count) < 0) return -1;
#endif
    return setenv("bootargs", commandline);
}
static unsigned char scratch[2 * H432B_ENV_BYTES];
static int read_copy(void *context, unsigned copy, unsigned char *buffer)
{
    (void)context;
    return h432b_ubi_bootstate_read(copy, buffer);
}
static int replace_copy(void *context, unsigned copy, const unsigned char *buffer)
{
    (void)context;
    return h432b_ubi_bootstate_replace(copy, buffer);
}
static int do_u2bootstate(cmd_tbl_t *cmdtp, int flag, int argc, char *const argv[])
{
    struct h432b_bootstore store = {NULL, read_copy, replace_copy};
    struct h432b_bootstate state;
    int selected, slot;
    (void)cmdtp; (void)flag;
    if (argc != 2 || (strcmp(argv[1], "inspect") &&
                     strcmp(argv[1], "consume") && strcmp(argv[1], "boot"))) return CMD_RET_USAGE;
    boot_started = get_timer(0);
#ifdef CONFIG_H432B_NAND_TIMING
    h432b_nand_timing_reset();
#endif
#ifdef CONFIG_H432B_REUSE_UBI
    {
        extern int h432b_ubi_attach_linux(void);
        if (h432b_ubi_attach_linux()) return CMD_RET_FAILURE;
    }
#else
    if (run_command("mtdparts default", 0) ||
        run_command("ubi part linux", 0)) return CMD_RET_FAILURE;
#endif
    attach_ms = get_timer(boot_started);
#ifdef CONFIG_H432B_NAND_TIMING
    h432b_nand_timing_kernel();
#endif
    selected = h432b_bootstate_load(&store, scratch, &state);
    if (selected < 0) {
        puts("No valid bootstate; remain in maintenance, no default reset\n");
        return CMD_RET_FAILURE;
    }
    printf("bootstate copy=%d serial=%u order=%c %c A=%u B=%u\n",
           selected, state.serial, 'A' + state.order[0], 'A' + state.order[1],
           state.left[0], state.left[1]);
    if (!strcmp(argv[1], "inspect")) return CMD_RET_SUCCESS;
    /* Each returned slot has already consumed a durably stored attempt. */
    slot = h432b_bootstate_attempt(&store, scratch);
    if (slot < 0) {
        printf("Bootstate attempt refused: %d\n", slot);
        return CMD_RET_FAILURE;
    }
    if (!strcmp(argv[1], "consume")) {
        printf("Persisted attempt for slot %c; no image launched\n", 'A' + slot);
        return CMD_RET_SUCCESS;
    }
    /* Returning from boot means failure before kernel entry. */
    for (selected = 0; selected < 510; selected++) {
        char command[32];
        if (h432b_bootstate_load(&store, scratch, &state) < 0) break;
        boot_slot = slot;
        boot_serial = state.serial;
        snprintf(command, sizeof(command), "u2nandboot %c", 'a' + slot);
        kernel_started = get_timer(0);
        run_command(command, 0);
        boot_slot = -1;
        setenv("bootargs", NULL);
        slot = h432b_bootstate_attempt(&store, scratch);
        if (slot < 0) break;
    }
    puts("No bootable attempt remains; USB maintenance required\n");
    return CMD_RET_FAILURE;
}
U_BOOT_CMD(u2bootstate, 2, 0, do_u2bootstate,
           "inspect or explicitly consume persistent RAUC boot state",
           "inspect|consume|boot");

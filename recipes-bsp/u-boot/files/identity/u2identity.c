// SPDX-License-Identifier: GPL-2.0-or-later
/* Preserve factory identity without reading or writing NAND ourselves. */
#include <common.h>
#include <libfdt.h>
#include <asm/io.h>
#include "identity.h"
#include "usb-identity.h"

static unsigned char factory_mac[6];
static int factory_valid;
static char board_id[32];

const char *u2_identity_board_id(void)
{
    return factory_valid ? board_id : NULL;
}

void u2_identity_capture(void)
{
    unsigned char args[H432B_ARGS_BYTES];
    unsigned i;
    /* Both bootstrap and high-RAM stage leave this low-RAM handoff intact.
     * Copy before Linux can reuse it. Never follow firmware RAM pointers. */
    for (i = 0; i < sizeof(args); i++)
        args[i] = readb((void *)(0x40000800UL + i));
    factory_valid = !h432b_factory_mac(args, sizeof(args), factory_mac);
    if (factory_valid)
        snprintf(board_id, sizeof(board_id),
                 "FM-H432B-MAC-%02X%02X%02X%02X%02X%02X",
                 factory_mac[0], factory_mac[1], factory_mac[2],
                 factory_mac[3], factory_mac[4], factory_mac[5]);
    else
        puts("Factory Ethernet identity unavailable; no identity invented\n");
}

/* Standard ARM boot preparation calls this after expanding the FDT.
 * Update both properties: Linux prefers mac-address when both are present. */
void ft_board_setup(void *blob, bd_t *bd)
{
    char commandline[H432B_KERNEL_COMMAND_LINE_SIZE];
    const char *args;
    int node, ret, len;

    /* fdt_chosen() has already copied the environment's final A/B arguments.
     * Set USB identity here for both NAND and explicit RAM Linux boots. */
    node = fdt_path_offset(blob, "/chosen");
    args = node < 0 ? NULL : fdt_getprop(blob, node, "bootargs", &len);
    if (!args || len < 1 ||
        h432b_usb_bootargs(args, len, u2_identity_board_id(),
                          commandline, sizeof(commandline))) {
        puts("USB identity not applied: invalid or oversized kernel arguments\n");
    } else {
        ret = fdt_setprop_string(blob, node, "bootargs", commandline);
        if (ret)
            printf("USB identity FDT fixup failed: %s\n", fdt_strerror(ret));
    }

    if (!factory_valid)
        return;
    node = fdt_path_offset(blob, "/ethernet@a8000000");
    if (node < 0 ||
        fdt_node_check_compatible(blob, node, "smsc,lan9115")) {
        puts("Factory MAC not applied: expected Ethernet node missing\n");
        return;
    }
    ret = fdt_setprop(blob, node, "mac-address", factory_mac, 6);
    if (!ret)
        ret = fdt_setprop(blob, node, "local-mac-address", factory_mac, 6);
    if (!ret)
        ret = fdt_setprop_string(blob, 0, "fractalmicro,board-id", board_id);
    if (!ret)
        ret = fdt_setprop_string(blob, 0, "fractalmicro,board-id-source",
                                "factory-ethernet-mac");
    if (ret)
        printf("Factory identity FDT fixup failed: %s\n", fdt_strerror(ret));
    else
        puts("Factory Ethernet MAC and MAC-derived board ID passed to Linux\n");
}

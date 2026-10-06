/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Experimental INFORM7 protocol; not Android BCB or slot selection. */
#ifndef H432B_BOOTMODE_H
#define H432B_BOOTMODE_H
#define H432B_MODE_NORMAL 0x48344e4dU
#define H432B_MODE_FASTBOOT 0x48344642U
/* 0: normal; 1: consumed fastboot; -1: clear failed, stay in maintenance.
 * Unknown values are not ours and must never be overwritten.
 */
static int h432b_take_bootmode(unsigned (*read_value)(void *),
                              void (*write_value)(void *, unsigned),
                              void *context)
{
    unsigned value = read_value(context);
    if (value != H432B_MODE_NORMAL && value != H432B_MODE_FASTBOOT)
        return 0;
    write_value(context, 0);
    if (read_value(context) != 0)
        return -1;
    return value == H432B_MODE_FASTBOOT ? 1 : 0;
}
#endif

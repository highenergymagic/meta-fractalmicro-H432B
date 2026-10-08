/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef H432B_NAND_WRITE_BOUNDARY_H
#define H432B_NAND_WRITE_BOUNDARY_H
/* Same Linux-format NAND pool as the runtime kernel, excluding both ends. */
static int h432b_nand_write_allowed(unsigned long long offset,
                                    unsigned long long length, unsigned align)
{
    return length && (align == 2048 || align == 131072) &&
           offset >= 0x00400000ULL && offset < 0x1ff00000ULL &&
           length <= 0x1ff00000ULL - offset &&
           !(offset % align) && !(length % align);
}
#endif

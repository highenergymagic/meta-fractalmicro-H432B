/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef H432B_FACTORY_IDENTITY_H
#define H432B_FACTORY_IDENTITY_H
/* Pure parser shared by freestanding U-Boot and native tests.
 * EBOOT ARGS v1.1, physical alias of the CE handoff at 0xa0000800.
 * Do not interpret CE's CPU-model-derived boot name as a unique serial. */
#define H432B_ARGS_BYTES 0x50
static int h432b_factory_mac(const unsigned char *args, unsigned size,
                            unsigned char mac[6])
{
    static const unsigned char header[8] = {'A','R','G','S',1,0,1,0};
    static const unsigned char fallback[6] = {0x6e,0x07,0x5d,0xd0,0,0x0b};
    unsigned i, any = 0, different = 0;
    if (size < H432B_ARGS_BYTES)
        return -1;
    for (i = 0; i < sizeof(header); i++)
        if (args[i] != header[i])
            return -1;
    if (args[0x48] & 1)
        return -1;
    for (i = 0; i < 6; i++) {
        any |= args[0x48+i];
        different |= args[0x48+i] ^ fallback[i];
    }
    if (!any || !different)
        return -1;
    for (i = 0; i < 6; i++)
        mac[i] = args[0x48+i];
    return 0;
}
#endif

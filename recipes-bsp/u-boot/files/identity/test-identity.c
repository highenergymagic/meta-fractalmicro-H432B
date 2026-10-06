// SPDX-License-Identifier: GPL-2.0-or-later
#include <assert.h>
#include <string.h>
#include "identity.h"
int main(void)
{
    unsigned char args[H432B_ARGS_BYTES] = {'A','R','G','S',1,0,1,0};
    const unsigned char mac[6] = {0x02,0x11,0x22,0x33,0x44,0x55};
    const unsigned char fallback[6] = {0x6e,0x07,0x5d,0xd0,0,0x0b};
    unsigned char output[6] = {0};
    unsigned i;
    memcpy(args+0x48, mac, 6);
    assert(h432b_factory_mac(args,sizeof(args),output) == 0);
    assert(!memcmp(mac,output,6));
    for (i=0; i<sizeof(args); i++)
        assert(h432b_factory_mac(args,i,output) == -1);
    for (i=0; i<8; i++) {
        args[i] ^= 0x80;
        assert(h432b_factory_mac(args,sizeof(args),output) == -1);
        args[i] ^= 0x80;
    }
    args[0x48] |= 1;
    assert(h432b_factory_mac(args,sizeof(args),output) == -1);
    memset(args+0x48,0xff,6);
    assert(h432b_factory_mac(args,sizeof(args),output) == -1);
    memset(args+0x48,0,6);
    assert(h432b_factory_mac(args,sizeof(args),output) == -1);
    memcpy(args+0x48,fallback,6);
    assert(h432b_factory_mac(args,sizeof(args),output) == -1);
    assert(!memcmp(mac,output,6)); /* Rejected inputs never modify output. */
    return 0;
}

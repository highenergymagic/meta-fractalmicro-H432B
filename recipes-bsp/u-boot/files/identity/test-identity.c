// SPDX-License-Identifier: GPL-2.0-or-later
#include <assert.h>
#include <string.h>
#include "identity.h"
#include "usb-identity.h"

static void test_usb_identity(void)
{
    /* Synthetic locally administered address, not device evidence. */
    const char *id = "FM-H432B-MAC-021122334455";
    const char *legacy = "console=tty0 g_serial.iProduct=OpenH432-RAM "
        "g_serial.iSerialNumber=H432-RAM-TEST rauc.slot=A openh432.attempt=42";
    const char *quoted = "console=tty0 g_serial.iProduct=\"old product\" "
        "\"g_serial.iManufacturer=old name\" option=\"keep this\" -- initarg";
    char out[512], again[512], tiny[16];
    char full[H432B_KERNEL_COMMAND_LINE_SIZE];
    char input[H432B_KERNEL_COMMAND_LINE_SIZE];
    unsigned i, prefix_length;

    assert(h432b_usb_bootargs(legacy, strlen(legacy)+1, id, out, sizeof(out)) == 0);
    assert(strstr(out, "rauc.slot=A openh432.attempt=42"));
    assert(strstr(out, "g_serial.iManufacturer=\"Fractal Microsystems\""));
    assert(strstr(out, "g_serial.iProduct=\"Braille Sense U2 (Linux)\""));
    assert(strstr(out, "g_serial.iSerialNumber=FM-H432B-MAC-021122334455"));
    assert(!strstr(out, "H432-RAM-TEST"));
    assert(h432b_usb_bootargs(out, strlen(out)+1, id, again, sizeof(again)) == 0);
    assert(!strcmp(out, again));
    assert(h432b_usb_bootargs(quoted, strlen(quoted)+1, NULL, out, sizeof(out)) == 0);
    assert(strstr(out, "option=\"keep this\""));
    assert(strstr(out, "(Linux)\" -- initarg"));
    assert(!strstr(out, "iSerialNumber"));
    assert(!strstr(out, "old name"));
    assert(!strstr(out, "old product"));
    for (i = 0; i < strlen(legacy)+1; i++)
        assert(h432b_usb_bootargs(legacy, i, id, out, sizeof(out)) == -1);
    assert(h432b_usb_bootargs(legacy, strlen(legacy)+1, id, tiny, sizeof(tiny)) == -1);
    assert(h432b_usb_bootargs("x\0y", 4, id, out, sizeof(out)) == -1);
    assert(h432b_usb_bootargs("x=\"y", 5, id, out, sizeof(out)) == -1);
    assert(h432b_usb_bootargs("", 1, "bad id", out, sizeof(out)) == -1);
    assert(h432b_usb_bootargs("", 1, NULL, out, sizeof(out)) == 0);
    assert(h432b_usb_bootargs("", 1, id, full, sizeof(full)) == 0);
    prefix_length = sizeof(full) - strlen(full) - 2;
    memset(input, 'x', prefix_length);
    input[prefix_length] = '\0';
    assert(h432b_usb_bootargs(input, prefix_length+1, id, full, sizeof(full)) == 0);
    assert(strlen(full) == sizeof(full)-1);
    input[prefix_length] = 'x';
    input[prefix_length+1] = '\0';
    assert(h432b_usb_bootargs(input, prefix_length+2, id, full, sizeof(full)) == -1);
}
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
    test_usb_identity();
    return 0;
}

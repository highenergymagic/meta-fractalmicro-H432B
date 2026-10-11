# SPDX-License-Identifier: MIT
"""Native tests of HT negotiation, RX rate decoding and sequence windows."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
FILES = ROOT / "recipes-kernel/linux/files"

PREFIX = r"""
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
#define BIT(n) (1U << (n))
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
static u16 get_unaligned_le16(const void *p)
{
    const u8 *b = p;
    return b[0] | b[1] << 8;
}
static void put_unaligned_le16(u16 n, void *p)
{
    u8 *b = p;
    b[0] = n; b[1] = n >> 8;
}
"""


@unittest.skipUnless(os.environ.get("WIFI_RX_NATIVE_CC"),
                     "native C tests require the pinned container compiler")
class WifiHt(unittest.TestCase):
    def compile(self, headers, vectors):
        source_text = PREFIX + "\n".join((FILES / h).read_text() for h in headers) + vectors
        with tempfile.TemporaryDirectory(prefix="wifi-ht-") as directory:
            source = Path(directory) / "test.c"
            binary = Path(directory) / "test"
            source.write_text(source_text)
            subprocess.run([os.environ["WIFI_RX_NATIVE_CC"], "-std=c11", "-O2",
                            "-Wall", "-Wextra", "-Werror", "-fsanitize=undefined",
                            str(source), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

    def test_capabilities_ies_rates(self):
        self.compile(["h432b-wifi-ht.h"], r"""
int main(void)
{
    struct wifi_ht_profile p;
    u8 ies[128] = {221, 7, 0, 0x50, 0xf2, 2, 0, 1, 0, 45, 26};
    u8 output[64] = {0}, rsn[] = {48, 2, 1, 0};
    unsigned int i;
    assert(wifi_rx_ht_mcs(BIT(6) | 12) == 0);
    assert(wifi_rx_ht_mcs(BIT(6) | 27) == 15);
    assert(wifi_rx_ht_mcs(BIT(6) | 28) == -1);
    assert(wifi_rx_ht_mcs(19) == -1);
    wifi_ht_capability(ies + 11);
    assert(wifi_ht_select(ies, 37, &p) == 0 && p.qos && p.ht);
    assert(get_unaligned_le16(p.capability) == 0x0020);
    assert(p.capability[2] == 3 && p.capability[3] == 255);
    assert(p.capability[4] == 255 && p.capability[15] == 3);
    assert((p.capability[15] & 12) == 0); /* One TX stream, not two. */
    assert(wifi_ht_join_ies(output, &p) == 37);
    assert(output[0] == 221 && output[6] == 0 && output[7] == 1);
    assert(output[9] == 45 && output[10] == 26);
    ies[11] &= ~2U;
    assert(wifi_ht_select(ies, 37, &p) == 0 && !(p.capability[0] & 2));
    assert(wifi_ht_select(ies, 9, &p) == 0 && p.qos && !p.ht);
    assert(wifi_ht_join_ies(output, &p) == 9);
    assert(wifi_ht_select(ies + 9, 28, &p) == 0 && !p.qos && !p.ht);
    assert(wifi_ht_join_ies(output, &p) == 0);
    assert(wifi_ht_select(ies, 0, &p) == 0 && !p.qos && !p.ht);
    assert(wifi_ht_select(ies, 1, &p) == -EBADMSG);
    for (i = 10; i < 37; i++)
        assert(wifi_ht_select(ies, i, &p) == -EBADMSG);
    memcpy(ies + 37, ies + 9, 28);
    assert(wifi_ht_select(ies, 65, &p) == -EBADMSG); /* duplicate HT */
    ies[10] = 25;
    assert(wifi_ht_select(ies, 36, &p) == -EBADMSG);
    assert(wifi_ht_assoc_ies_valid(rsn, sizeof(rsn)));
    assert(!wifi_ht_assoc_ies_valid(rsn, 3));
    assert(!wifi_ht_assoc_ies_valid(ies, 9)); /* host owns WMM */
    assert(!wifi_ht_assoc_ies_valid(ies + 9, 27));
    assert(!wifi_ht_assoc_ies_valid(NULL, WIFI_ASSOC_IE_MAX + 1));
    assert(wifi_rx_legacy_rate(0) == 10);
    assert(wifi_rx_legacy_rate(2) == 55);
    assert(wifi_rx_legacy_rate(11) == 540);
    assert(wifi_rx_legacy_rate(11 | BIT(6)) == 0);
    assert(wifi_rx_legacy_rate(12) == 0);
    assert(wifi_rx_legacy_rate(63) == 0);
    assert(wifi_rx_legacy_rate(11 | BIT(14)) == 540); /* SDIO is not USB */
    return 0;
}
""")

    def test_sequence_window_wrap_gaps_duplicates(self):
        self.compile(["h432b-wifi-reorder.h"], r"""
struct delivered { unsigned int values[8192]; unsigned int count; };
static void release(void *context, void *frame)
{
    struct delivered *d = context;
    assert(d->count < ARRAY_SIZE(d->values));
    d->values[d->count++] = (uintptr_t)frame - 1;
}
static void *packet(unsigned int sequence) { return (void *)(uintptr_t)(sequence + 1); }
int main(void)
{
    struct wifi_reorder w = {0};
    struct delivered d = {0};
    unsigned int i;
    /* Keep the common prefix functions used with -Werror. */
    u8 b[2]; put_unaligned_le16(0x1234, b); assert(get_unaligned_le16(b) == 0x1234);
    assert(wifi_reorder_insert(&w, 4094, packet(4094), release, &d));
    assert(w.head == 4095 && d.count == 1);
    assert(wifi_reorder_insert(&w, 0, packet(0), release, &d));
    assert(w.pending == 1 && d.count == 1);
    assert(!wifi_reorder_insert(&w, 0, packet(0), release, &d));
    assert(wifi_reorder_insert(&w, 4095, packet(4095), release, &d));
    assert(w.head == 1 && w.pending == 0 && d.count == 3);
    assert(d.values[1] == 4095 && d.values[2] == 0);
    assert(!wifi_reorder_insert(&w, 4094, packet(4094), release, &d));
    assert(!wifi_reorder_insert(&w, 2049, packet(2049), release, &d));
    assert(wifi_reorder_insert(&w, 3, packet(3), release, &d));
    wifi_reorder_expire(&w, release, &d);
    assert(w.head == 4 && w.pending == 0 && d.values[3] == 3);
    assert(wifi_reorder_insert(&w, 6, packet(6), release, &d));
    assert(wifi_reorder_insert(&w, 70, packet(70), release, &d));
    assert(w.head == 7 && w.pending == 1 && d.values[4] == 6);
    wifi_reorder_clear(&w, release, &d);
    assert(!w.started && !w.enabled && !w.pending && d.values[5] == 70);
    memset(&d, 0, sizeof(d));
    /* Reverse every bounded window, crossing sequence wrap twice. */
    assert(wifi_reorder_insert(&w, 0, packet(0), release, &d));
    for (i = 1; i < 8000; i += 63) {
        unsigned int j, end = i + 62;
        if (end >= 8000) end = 7999;
        for (j = end; j >= i; j--)
            assert(wifi_reorder_insert(&w, j & 4095, packet(j), release, &d));
        assert(w.pending == 0);
    }
    assert(d.count == 8000);
    for (i = 0; i < d.count; i++) assert(d.values[i] == i);
    wifi_reorder_clear(&w, release, &d);
    return 0;
}
""")


if __name__ == "__main__":
    unittest.main()

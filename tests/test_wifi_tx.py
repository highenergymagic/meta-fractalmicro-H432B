# SPDX-License-Identifier: MIT
"""Execute production rate-policy and descriptor code, without a radio."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

FILES = Path(__file__).resolve().parents[1] / "recipes-kernel/linux/files"


class WifiTransmit(unittest.TestCase):
    def test_driver_delegates_normal_rate_to_firmware(self):
        data = (FILES / "h432b-wifi-data.h").read_text()
        self.assertIn("wifi_tx_descriptor(packet, length, basic, qos, tid,", data)
        self.assertIn("get_unaligned_le16(frame + 22) >> 4", data)
        self.assertIn("basic = wifi_tx_basic_rate(skb->data, skb->len)", data)
        self.assertNotIn("0x001f8000", data)
        net = (FILES / "h432b-wifi-net.h").read_text()
        self.assertNotIn("mutex_lock(&net->lock)", net)
        self.assertIn("spin_lock_irqsave(&net->tx_queue.lock", net)
        self.assertIn("spin_lock_irqsave(&net->tx_queue.lock", data)

    @unittest.skipUnless(os.environ.get("WIFI_RX_NATIVE_CC"), "requires pinned compiler")
    def test_native_rate_and_descriptor_vectors(self):
        source = r"""
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
typedef uint8_t u8;
typedef uint16_t u16;
#define BIT(n) (1U << (n))
#define ETH_HLEN 14
#define ETH_P_PAE 0x888e
#define ETH_P_ARP 0x0806
#define ETH_P_IP 0x0800
#define IPPROTO_UDP 17
static u16 get_unaligned_be16(const void *p) {
    const u8 *b = p; return (b[0] << 8) | b[1];
}
static void put_unaligned_le32(uint32_t n, void *p) {
    u8 *b = p; b[0] = n; b[1] = n >> 8; b[2] = n >> 16; b[3] = n >> 24;
}
""" + (FILES / "h432b-wifi-tx.h").read_text() + r"""
int main(void) {
    u8 packet[128] = {0}, descriptor[32];
    packet[12] = 0x88; packet[13] = 0x8e;
    assert(wifi_tx_basic_rate(packet, 14));
    assert(!wifi_tx_basic_rate(packet, 13));
    packet[12] = 8; packet[13] = 6;
    assert(wifi_tx_basic_rate(packet, 14));
    packet[13] = 0; packet[14] = 0x45; packet[23] = 17;
    packet[35] = 68; packet[37] = 67;
    assert(wifi_tx_basic_rate(packet, 42));
    assert(!wifi_tx_basic_rate(packet, 41));
    packet[35] = 67; packet[37] = 68;
    assert(wifi_tx_basic_rate(packet, 42));
    packet[20] = 0x20; /* fragmented DHCP must not read arbitrary fragment data */
    assert(!wifi_tx_basic_rate(packet, 42));
    packet[20] = 0; packet[14] = 0x44;
    assert(!wifi_tx_basic_rate(packet, 42));
    packet[14] = 0x45; packet[23] = 6;
    assert(!wifi_tx_basic_rate(packet, 42)); /* normal TCP */
    packet[12] = 0x86; packet[13] = 0xdd;
    assert(!wifi_tx_basic_rate(packet, 128)); /* IPv6 */
    memset(descriptor, 0xa5, sizeof(descriptor));
    wifi_tx_descriptor(descriptor, 1500, false, false, 0, 0);
    assert(descriptor[0] == 0xdc && descriptor[1] == 5);
    assert(descriptor[2] == 32 && descriptor[3] == 0x8c);
    assert(descriptor[4] == 5 && descriptor[6] == 1);
    for (unsigned i = 8; i < sizeof(descriptor); i++) assert(!descriptor[i]);
    wifi_tx_descriptor(descriptor, 128, true, false, 0, 0);
    assert(descriptor[19] == 0x80);
    assert(descriptor[20] == 0 && descriptor[21] == 0x80 && descriptor[22] == 0x1f);
    for (unsigned i = 0; i < 8; i++) {
        /* Firmware mode 1 copies descriptor bits 27:16 to the on-air
         * sequence; test every wrap value independently of the queue TID.
         */
        for (unsigned int seq = 0; seq <= 4096; seq++) {
            wifi_tx_descriptor(descriptor, 1500, false, true, i, seq);
            assert(descriptor[4] == 5 && descriptor[5] == i && descriptor[6] == 0);
            assert(descriptor[14] == (seq & 255));
            assert(descriptor[15] == ((seq >> 8) & 15) && descriptor[19] == 0);
        }
    }
    assert(wifi_tx_fifo(0) == 0x18dc0 && wifi_tx_free_pages(0) == 7);
    assert(wifi_tx_fifo(1) == 0x18e00 && wifi_tx_free_pages(1) == 8);
    assert(wifi_tx_fifo(4) == 0x18d80 && wifi_tx_free_pages(4) == 6);
    assert(wifi_tx_fifo(7) == 0x18d40 && wifi_tx_free_pages(7) == 5);
    return 0;
}
"""
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "test.c"
            binary = Path(directory) / "test"
            path.write_text(source)
            subprocess.run([os.environ["WIFI_RX_NATIVE_CC"], "-std=c11", "-Wall",
                            "-Wextra", "-Werror", str(path), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

# SPDX-License-Identifier: MIT
"""Run the actual RX admission/reorder/delivery code on crafted 802.11 frames.

Crypto and networking are test doubles. These vectors test ownership and
security decision ordering, not AES implementation or the upstream A-MSDU
parser. CCMP wire/crypto-request construction has separate native vectors.
"""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

FILES = Path(__file__).resolve().parents[1] / "recipes-kernel/linux/files"

PREFIX = r"""
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <errno.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
#define BIT(n) (1U << (n))
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define ETH_ALEN 6
#define ETH_P_PAE 0x888e
#define NET_IP_ALIGN 2
#define GFP_KERNEL 0
#define NL80211_IFTYPE_STATION 2
#define WLAN_CIPHER_SUITE_CCMP 0xfac04
#define CRYPTO_ALG_ASYNC 1
#define IS_ERR(p) false
#define PTR_ERR(p) (-ENOMEM)
#define BUILD_BUG_ON(x) _Static_assert(!(x), "size")
struct stats { unsigned int rx_packets, rx_bytes, rx_dropped; };
struct net_device { u8 dev_addr[6]; struct stats stats; };
struct sk_buff {
    u8 *head, *data;
    unsigned int len, priority, protocol;
    struct net_device *dev;
    _Alignas(8) u8 cb[48];
    struct sk_buff *next;
};
struct sk_buff_head { struct sk_buff *first, *last; };
static unsigned int allocated, delivered, amsdu_calls, timer_calls;
static bool mic_failure, amsdu_failure;
static unsigned long jiffies = 100;
static unsigned int transforms;
static int key_error;
struct crypto_aead { int unused; };
static struct crypto_aead *crypto_alloc_aead(const char *name, int type, int mask)
{ assert(!strcmp(name, "ccm(aes)") && type == 0 && mask == CRYPTO_ALG_ASYNC);
  transforms++; return calloc(1, sizeof(struct crypto_aead)); }
static int crypto_aead_setkey(struct crypto_aead *tfm, const void *key, unsigned int len)
{ assert(tfm && key && len == 16); return key_error; }
static int crypto_aead_setauthsize(struct crypto_aead *tfm, unsigned int size)
{ assert(tfm && size == 8); return 0; }
static void crypto_free_aead(struct crypto_aead *tfm) { assert(transforms); transforms--; free(tfm); }
static void memzero_explicit(void *p, unsigned int n) { memset(p, 0, n); }
static void mutex_lock(void *p) { assert(p); }
static void mutex_unlock(void *p) { assert(p); }
static void cancel_delayed_work(void *p) { (void)p; }
struct wiphy { void *private; };
static void *wiphy_priv(struct wiphy *w) { return w->private; }
struct key_params { unsigned int cipher, key_len, seq_len; const u8 *key, *seq; };
static struct sk_buff *alloc_skb(unsigned int length, int flags)
{
    struct sk_buff *s = calloc(1, sizeof(*s));
    (void)flags;
    assert(s);
    s->data = s->head = calloc(1, length);
    assert(s->head);
    allocated++;
    return s;
}
static void skb_reserve(struct sk_buff *s, unsigned int n) { s->data += n; }
static void *skb_put_data(struct sk_buff *s, const void *p, unsigned int n)
{
    void *start = s->data + s->len;
    memcpy(start, p, n); s->len += n; return start;
}
static void skb_pull(struct sk_buff *s, unsigned int n)
{ assert(n <= s->len); s->data += n; s->len -= n; }
static void skb_push(struct sk_buff *s, unsigned int n)
{ assert(s->data >= s->head + n); s->data -= n; s->len += n; }
static void skb_trim(struct sk_buff *s, unsigned int n) { assert(n <= s->len); s->len = n; }
static void dev_kfree_skb(struct sk_buff *s)
{ allocated--; free(s->head); free(s); }
static unsigned int eth_type_trans(struct sk_buff *s, struct net_device *d)
{ (void)d; assert(s->len >= 14); return s->data[12] << 8 | s->data[13]; }
static void netif_rx(struct sk_buff *s) { delivered++; dev_kfree_skb(s); }
static void __skb_queue_head_init(struct sk_buff_head *q) { memset(q, 0, sizeof(*q)); }
static bool skb_queue_empty(struct sk_buff_head *q) { return !q->first; }
static struct sk_buff *__skb_dequeue(struct sk_buff_head *q)
{ struct sk_buff *s = q->first; if (s) q->first = s->next; return s; }
static void ieee80211_amsdu_to_8023s(struct sk_buff *s, struct sk_buff_head *q,
    const u8 *addr, unsigned int type, unsigned int extra,
    const u8 *da, const u8 *sa, u8 mesh)
{
    assert(addr && type == NL80211_IFTYPE_STATION && !extra && da && !sa && !mesh);
    amsdu_calls++;
    if (amsdu_failure) { dev_kfree_skb(s); return; }
    assert(s->len >= 14);
    q->first = q->last = s;
}
static u16 get_unaligned_le16(const void *p)
{ const u8 *b = p; return b[0] | b[1] << 8; }
static u16 get_unaligned_be16(const void *p)
{ const u8 *b = p; return b[1] | b[0] << 8; }
static void put_unaligned_le16(u16 n, void *p)
{ u8 *b = p; b[0] = n; b[1] = n >> 8; }
static bool ether_addr_equal(const void *a, const void *b) { return !memcmp(a, b, 6); }
static bool is_multicast_ether_addr(const u8 *p) { return p[0] & 1; }
static unsigned long msecs_to_jiffies(unsigned int n) { return n; }
static void queue_delayed_work(void *q, void *w, unsigned long delay)
{ (void)q; (void)w; assert(delay == 30); timer_calls++; }
struct h432b_wifi_key { void *tfm; u64 rx_pn[17], tx_pn; u8 material[16]; };
static u64 wifi_ccmp_pn(const u8 *iv)
{ return (u64)iv[0] | (u64)iv[1] << 8 | (u64)iv[4] << 16 |
         (u64)iv[5] << 24 | (u64)iv[6] << 32 | (u64)iv[7] << 40; }
static int wifi_ccmp_crypt(struct h432b_wifi_key *key, u8 *frame,
    unsigned int header, unsigned int payload, bool decrypt)
{
    assert(key->tfm && frame && (header == 24 || header == 26) && payload >= 8 && decrypt);
    return mic_failure ? -EBADMSG : 0;
}
"""

STATE = r"""
struct h432b_wifi_net {
    struct test_owner { int lock; } *owner;
    struct net_device *dev;
    struct h432b_wifi_key keys[5];
    struct wifi_ht_profile ht;
    struct wifi_reorder reorder[16];
    unsigned long reorder_deadline[16];
    void *workqueue;
    unsigned int reorder_work;
    u8 bssid[6];
    bool associated, authorized;
    unsigned int rx_replay, rx_rejected, rx_mic, rx_reorder_dropped, rx_legacy_rate;
    u64 rx_ht_authenticated, rx_ht_mcs[16], rx_amsdu_authenticated;
};
"""

VECTORS = r"""
static unsigned int packet(u8 *p, bool encrypted, bool qos, bool amsdu,
    u16 seq, u64 pn, u16 protocol, const struct h432b_wifi_net *n)
{
    unsigned int header = qos ? 26 : 24, offset = header;
    memset(p, 0, 512);
    put_unaligned_le16(0x0208 | (encrypted ? 0x4000 : 0) | (qos ? 0x80 : 0), p);
    memcpy(p + 4, n->dev->dev_addr, 6);
    memcpy(p + 10, n->bssid, 6);
    p[16] = 2; p[21] = 9;
    put_unaligned_le16(seq << 4, p + 22);
    if (qos) p[24] = 5 | (amsdu ? 0x80 : 0);
    if (encrypted) {
        p[offset] = pn; p[offset + 1] = pn >> 8; p[offset + 3] = 0x20;
        offset += 8;
    }
    memcpy(p + offset, "\xaa\xaa\x03\x00\x00\x00", 6);
    p[offset + 6] = protocol >> 8; p[offset + 7] = protocol;
    return offset + 32 + (encrypted ? 8 : 0);
}
int main(void)
{
    struct net_device dev = { .dev_addr = {2, 1, 2, 3, 4, 5} };
    struct h432b_wifi_net n = { .dev = &dev, .associated = true, .authorized = true,
        .bssid = {2, 6, 7, 8, 9, 10}, .ht = { .qos = true, .ht = true } };
    u8 p[512];
    unsigned int length, before;
    n.keys[4].tfm = &n;
    length = packet(p, false, false, false, 1, 0, 0x0800, &n);
    wifi_net_receive(&n, p, length, 11); assert(delivered == 0 && allocated == 0);
    length = packet(p, false, false, false, 1, 0, ETH_P_PAE, &n);
    wifi_net_receive(&n, p, length, 11); assert(delivered == 1 && allocated == 0);
    n.reorder[5].enabled = true;
    length = packet(p, true, true, false, 10, 10, 0x0800, &n);
    mic_failure = true;
    wifi_net_receive(&n, p, length, 11);
    assert(n.rx_mic == 1 && !n.reorder[5].started && !n.keys[4].rx_pn[5]);
    mic_failure = false;
    wifi_net_receive(&n, p, length, 11);
    assert(n.keys[4].rx_pn[5] == 10 && n.rx_legacy_rate == 540 && delivered == 2);
    length = packet(p, true, true, false, 12, 12, 0x0800, &n);
    wifi_net_receive(&n, p, length, 11);
    assert(n.keys[4].rx_pn[5] == 10 && allocated == 1 && timer_calls == 1);
    length = packet(p, true, true, false, 11, 11, 0x0800, &n);
    wifi_net_receive(&n, p, length, 11);
    assert(n.keys[4].rx_pn[5] == 12 && allocated == 0 && delivered == 4);
    wifi_net_receive(&n, p, length, 11); /* replay cannot enter sequence window */
    assert(n.rx_replay == 1 && delivered == 4 && allocated == 0);
    length = packet(p, true, true, false, 14, 14, 0x0800, &n);
    wifi_net_receive(&n, p, length, 11);
    wifi_net_receive(&n, p, length, 11); /* pending duplicate */
    assert(allocated == 1 && n.rx_reorder_dropped == 1);
    wifi_reorder_expire(&n.reorder[5], wifi_net_rx_release, &n);
    assert(allocated == 0 && n.keys[4].rx_pn[5] == 14);
    /* Same PN at another sequence passes the first check while queued, but
     * must fail the second replay check at delivery. */
    length = packet(p, true, true, false, 17, 20, 0x0800, &n);
    wifi_net_receive(&n, p, length, 11);
    length = packet(p, true, true, false, 16, 20, 0x0800, &n);
    wifi_net_receive(&n, p, length, 11);
    before = delivered;
    wifi_reorder_expire(&n.reorder[5], wifi_net_rx_release, &n);
    assert(delivered == before + 1 && n.rx_replay == 2 && allocated == 0);
    length = packet(p, true, true, false, 20, 21, 0x0800, &n);
    wifi_net_receive(&n, p, length, 11);
    wifi_reorder_clear(&n.reorder[5], wifi_net_rx_discard, &n);
    assert(allocated == 0 && delivered == before + 1);
    length = packet(p, true, true, true, 21, 22, 0x0800, &n);
    amsdu_failure = true;
    wifi_net_receive(&n, p, length, BIT(6) | 19);
    assert(amsdu_calls == 1 && n.keys[4].rx_pn[5] == 20 && !allocated);
    assert(!n.rx_legacy_rate); /* no guessed HT width/GI telemetry */
    assert(n.rx_ht_authenticated == 1 && n.rx_ht_mcs[7] == 1);
    amsdu_failure = false;
    wifi_net_receive(&n, p, length, BIT(6) | 19);
    assert(amsdu_calls == 2 && n.keys[4].rx_pn[5] == 22 && !allocated);
    assert(n.rx_ht_authenticated == 2 && n.rx_ht_mcs[7] == 2);
    length = packet(p, false, true, true, 22, 0, ETH_P_PAE, &n);
    wifi_net_receive(&n, p, length, 11);
    assert(amsdu_calls == 2 && !allocated); /* unauthenticated aggregate */
    length = packet(p, true, true, false, 22, 23, 0x0800, &n);
    n.authorized = false;
    wifi_net_receive(&n, p, length, 11);
    assert(n.keys[4].rx_pn[5] == 22 && !allocated);
    p[24] |= 0x80;
    wifi_net_receive(&n, p, length, 11);
    assert(amsdu_calls == 2 && !allocated);
    /* Header bounds and source filtering happen before crypto/reorder. */
    n.authorized = true;
    p[10] ^= 1;
    wifi_net_receive(&n, p, length, 11);
    wifi_net_receive(&n, p, 25, 11);
    wifi_net_receive(&n, p, WIFI_HT_MAX_AMSDU + 43, 11);
    assert(!allocated);
    /* Per-TID replay counters are independent, including non-QoS and GTK. */
    length = packet(p, true, true, false, 25, 1, 0x0800, &n);
    p[24] = 3;
    before = delivered;
    wifi_net_receive(&n, p, length, 11);
    assert(delivered == before + 1 && n.keys[4].rx_pn[3] == 1);
    assert(n.keys[4].rx_pn[5] == 22);
    length = packet(p, true, false, false, 25, 1, 0x0800, &n);
    wifi_net_receive(&n, p, length, 11);
    assert(n.keys[4].rx_pn[16] == 1 && n.keys[4].rx_pn[5] == 22);
    n.keys[1].tfm = &n;
    length = packet(p, true, true, false, 25, 1, 0x0800, &n);
    p[4] = 1; p[26 + 3] = 0x60; /* group key 1 */
    wifi_net_receive(&n, p, length, 11);
    assert(n.keys[1].rx_pn[5] == 1 && n.keys[4].rx_pn[5] == 22);
    /* Actual key callbacks: same-key reinstall preserves counters and queue;
     * failed replacement preserves the old key; success/deletion purge. */
    memset(n.keys, 0, sizeof(n.keys));
    struct test_owner owner = {0}; n.owner = &owner;
    struct wiphy w = { .private = &n };
    u8 material[16] = {1}, seq[6] = {7};
    struct key_params params = { .cipher = WLAN_CIPHER_SUITE_CCMP,
        .key_len = 16, .key = material, .seq = seq, .seq_len = 6 };
    assert(wifi_net_add_key(&w, &dev, -1, 0, true, n.bssid, &params) == 0);
    assert(transforms == 1 && n.keys[4].rx_pn[5] == 7);
    n.keys[4].tx_pn = 55;
    n.reorder[5].enabled = true;
    length = packet(p, true, true, false, 30, 8, 0x0800, &n);
    wifi_net_receive(&n, p, length, 11);
    length = packet(p, true, true, false, 32, 10, 0x0800, &n);
    wifi_net_receive(&n, p, length, 11);
    assert(allocated == 1 && n.reorder[5].pending == 1);
    void *old_tfm = n.keys[4].tfm;
    before = delivered;
    assert(wifi_net_add_key(&w, &dev, -1, 0, true, n.bssid, &params) == 0);
    assert(allocated == 1 && n.keys[4].tfm == old_tfm && transforms == 1);
    assert(n.keys[4].tx_pn == 55 && n.keys[4].rx_pn[5] == 8);
    material[0] = 2; key_error = -EINVAL;
    assert(wifi_net_add_key(&w, &dev, -1, 0, true, n.bssid, &params) == -EINVAL);
    assert(allocated == 1 && n.keys[4].tfm == old_tfm && transforms == 1);
    key_error = 0;
    assert(wifi_net_add_key(&w, &dev, -1, 0, true, n.bssid, &params) == 0);
    assert(!allocated && delivered == before && transforms == 1);
    assert(n.reorder[5].enabled && !n.reorder[5].started);
    assert(n.keys[4].tx_pn == 0 && n.keys[4].rx_pn[5] == 7);
    length = packet(p, true, true, false, 40, 8, 0x0800, &n);
    wifi_net_receive(&n, p, length, 11);
    length = packet(p, true, true, false, 42, 10, 0x0800, &n);
    wifi_net_receive(&n, p, length, 11);
    before = delivered;
    assert(allocated == 1);
    assert(wifi_net_del_key(&w, &dev, -1, 0, true, n.bssid) == 0);
    assert(!allocated && !transforms && delivered == before && !n.authorized);
    assert(n.reorder[5].enabled && !n.reorder[5].started);
    wifi_net_clear_keys(&n);
    assert(!n.reorder[5].enabled);
    return 0;
}
"""


class WifiRxFrames(unittest.TestCase):
    @unittest.skipUnless(os.environ.get("WIFI_RX_NATIVE_CC"),
                         "native tests require the pinned container compiler")
    def test_rx_security_order_and_ownership(self):
        data = (FILES / "h432b-wifi-data.h").read_text()
        helpers = data.split("struct wifi_rx_frame", 1)[1].split(
            "static void wifi_net_reorder_clear", 1)[0]
        receive = data.split("static void wifi_net_receive", 1)[1].split(
            "/* One packet per", 1)[0]
        clear = data.split("static void wifi_net_reorder_clear", 1)[1].split(
            "static void wifi_net_reorder_work", 1)[0]
        keys = data.split("static void wifi_net_clear_keys", 1)[1].split(
            "static int wifi_net_default_key", 1)[0]
        text = (PREFIX + (FILES / "h432b-wifi-ht.h").read_text() +
                (FILES / "h432b-wifi-reorder.h").read_text() + STATE +
                "struct wifi_rx_frame" + helpers + "static void wifi_net_reorder_clear" + clear +
                "static void wifi_net_clear_keys" + keys + "static void wifi_net_receive" +
                receive + VECTORS)
        with tempfile.TemporaryDirectory(prefix="wifi-frames-") as directory:
            source = Path(directory) / "test.c"
            binary = Path(directory) / "test"
            source.write_text(text)
            subprocess.run([os.environ["WIFI_RX_NATIVE_CC"], "-std=c11", "-O2",
                            "-Wall", "-Wextra", "-Werror", "-Wno-unused-function",
                            "-Wno-unused-parameter",
                            "-fsanitize=undefined", str(source), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()

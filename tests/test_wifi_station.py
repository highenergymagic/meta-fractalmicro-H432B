# SPDX-License-Identifier: MIT
"""Station protocol contracts and actual H2C framing vectors, not radio emulation."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

FILES = Path(__file__).resolve().parents[1] / "recipes-kernel/linux/files"
ASSOC = (FILES / "h432b-wifi-assoc.h").read_text()
DATA = (FILES / "h432b-wifi-data.h").read_text()
CCMP = (FILES / "h432b-wifi-ccmp.h").read_text()
H2C = (FILES / "h432b-wifi-h2c.h").read_text()
NET = (FILES / "h432b-wifi-net.h").read_text()


class WifiStation(unittest.TestCase):
    def test_security_scope(self):
        for token in ("NL80211_WPA_VERSION_2", "WLAN_CIPHER_SUITE_CCMP",
                      "WLAN_AKM_SUITE_PSK", "NL80211_MFP_NO", "wifi_ht_assoc_ies_valid"):
            self.assertIn(token, ASSOC)
        self.assertIn("wifi_write(net->func, 1, 0x250, 0", ASSOC)
        self.assertIn("REGULATORY_COUNTRY_IE_IGNORE", NET)
        self.assertIn('crypto_alloc_aead("ccm(aes)", 0, CRYPTO_ALG_ASYNC)', DATA)
        key_ops = DATA.split("static int wifi_net_add_key", 1)[1].split(
            "static int wifi_net_default_key", 1)[0]
        self.assertNotIn("wifi_h2c_send", key_ops)  # key material never goes to firmware

    def test_controlled_port_and_replay(self):
        self.assertIn("!net->authorized && skb->protocol != htons(ETH_P_PAE)", DATA)
        self.assertIn("(!metadata.encrypted || !net->authorized) && protocol != ETH_P_PAE", DATA)
        self.assertIn("pn <= key->rx_pn[tid]", DATA)
        self.assertIn("key->tx_pn >= 0xffffffffffffULL", DATA)
        self.assertLess(DATA.index("crypto_aead_setkey"), DATA.index("key->tfm = tfm"))
        rx = DATA.split("static void wifi_net_receive", 1)[1]
        self.assertLess(rx.index("wifi_ccmp_crypt"), rx.index("wifi_reorder_insert"))
        release = DATA.split("static void wifi_net_rx_release", 1)[1].split(
            "static void wifi_net_rx_discard", 1)[0]
        self.assertLess(release.index("metadata.pn <= key->rx_pn"),
                        release.index("key->rx_pn[metadata.tid] = metadata.pn"))
        self.assertLess(DATA.index("!memcmp(key->material"), DATA.index("memzero_explicit(key"))
        self.assertIn("CRYPTO_ALG_ASYNC", DATA)
        self.assertIn("aead_request_free(request)", CCMP)

    def test_no_unbounded_transmit_or_join(self):
        self.assertIn("tries < 100", DATA)
        self.assertIn("wifi_net_fault(net, error)", DATA)
        self.assertIn("net->join_timeout, 20 * HZ", ASSOC)
        self.assertIn("net->cache[i].seen + 30 * HZ", ASSOC)
        self.assertIn("memset(packet, 0, transfer)", H2C)
        self.assertIn("kfree_sensitive(packet)", H2C)

    @unittest.skipUnless(os.environ.get("WIFI_RX_NATIVE_CC"), "requires pinned compiler")
    def test_actual_h2c_packet_vectors(self):
        framing = H2C.split("static int wifi_h2c_send", 1)[0]
        prefix = r"""
#include <stdint.h>
#include <string.h>
#include <assert.h>
typedef uint8_t u8;
typedef uint32_t u32;
#define ALIGN(n,a) (((n)+(a)-1)&~((a)-1))
static void put_unaligned_le32(uint32_t n, void *p) {
    uint8_t *b=p; b[0]=n; b[1]=n>>8; b[2]=n>>16; b[3]=n>>24;
}
"""
        vectors = r"""
int main(void) {
    u8 packet[1024], input[884];
    memset(input, 0xa5, sizeof(input));
    memset(packet, 0xcc, sizeof(packet));
    assert(wifi_h2c_packet(packet, 14, 127, input, 884) == 1024);
    assert(packet[0]==0x80 && packet[1]==3 && packet[2]==0x20 && packet[3]==0x8c);
    assert(packet[32]==0x78 && packet[33]==3 && packet[34]==14 && packet[35]==127);
    assert(!memcmp(packet+40,input,884));
    for (unsigned int i=924;i<1024;i++) assert(packet[i]==0);
    assert(wifi_h2c_packet(packet,19,128,input,4)==512);
    assert(packet[32]==8 && packet[34]==19 && packet[35]==0);
    for (unsigned int i=44;i<512;i++) assert(packet[i]==0);
    return 0;
}
"""
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "test.c"
            binary = Path(directory) / "test"
            source.write_text(prefix + framing + vectors)
            subprocess.run([os.environ["WIFI_RX_NATIVE_CC"], "-std=c11", "-Wall",
                            "-Wextra", "-Werror", str(source), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

    @unittest.skipUnless(os.environ.get("WIFI_RX_NATIVE_CC"), "requires pinned compiler")
    def test_actual_ccmp_nonce_aad_and_pn(self):
        # Stub only the crypto provider; exercise production nonce/AAD code.
        # This validates framing, not AES itself or radio MIC handling.
        prefix = r"""
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>
#include <errno.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint64_t u64;
#define ETH_ALEN 6
#define GFP_KERNEL 0
struct crypto_aead { int unused; };
struct scatterlist { void *p; unsigned int len; };
struct aead_request { struct scatterlist *sg; unsigned int len, ad; u8 *nonce; };
static void *kzalloc(unsigned int n, int flags) { (void)flags; return calloc(1,n); }
static void kfree_sensitive(void *p) { free(p); }
static u16 get_unaligned_le16(const u8 *b) { return b[0] | b[1]<<8; }
static void put_unaligned_le16(u16 n, u8 *b) { b[0]=n; b[1]=n>>8; }
static struct aead_request *aead_request_alloc(struct crypto_aead *t, int f) {
    (void)t; (void)f; return calloc(1,sizeof(struct aead_request));
}
static void aead_request_free(struct aead_request *r) { free(r); }
static void sg_init_table(struct scatterlist *s, unsigned int n) { memset(s,0,n*sizeof(*s)); }
static void sg_set_buf(struct scatterlist *s, void *p, unsigned int n) { s->p=p; s->len=n; }
static void aead_request_set_ad(struct aead_request *r, unsigned int n) { r->ad=n; }
static void aead_request_set_crypt(struct aead_request *r, struct scatterlist *a,
                                 struct scatterlist *b, unsigned int n, u8 *iv) {
    assert(a==b); r->sg=a; r->len=n; r->nonce=iv;
}
static int check_request(struct aead_request *r, bool decrypt) {
    u8 *a=r->sg[0].p;
    assert(r->ad==(decrypt?24:22));
    assert(r->len==(decrypt?24:16));
    assert(r->sg[1].len==24);
    assert(a[0]==(decrypt?0x88:0x08) && a[1]==(decrypt?0x42:0x41));
    for (unsigned int i=0;i<18;i++) assert(a[i+2]==i+1);
    assert(a[20]==0 && a[21]==0);
    if (decrypt) assert(a[22]==5 && a[23]==0);
    assert(r->nonce[0]==1 && r->nonce[1]==(decrypt?5:0));
    for (unsigned int i=0;i<6;i++) {
        assert(r->nonce[2+i]==i+7);
        assert(r->nonce[8+i]==i+1);
    }
    return decrypt ? -EBADMSG : 0; /* propagate provider failure unchanged */
}
static int crypto_aead_encrypt(struct aead_request *r) { return check_request(r,false); }
static int crypto_aead_decrypt(struct aead_request *r) { return check_request(r,true); }
"""
        vectors = r"""
int main(void) {
    u8 frame[80]={0}, iv[8];
    struct h432b_wifi_key key={0};
    wifi_ccmp_iv(iv,0x010203040506ULL,2);
    assert(iv[0]==6 && iv[1]==5 && iv[2]==0 && iv[3]==0xa0);
    assert(wifi_ccmp_pn(iv)==0x010203040506ULL);
    frame[0]=0x78; frame[1]=0x79; /* subtype/retry/PM/more-data are masked */
    for (unsigned int i=0;i<18;i++) frame[4+i]=i+1;
    memcpy(frame+24,iv,8);
    assert(wifi_ccmp_crypt(&key,frame,24,16,false)==0);
    frame[0]=0xf8; frame[1]=0xfa; frame[24]=5; frame[25]=0;
    memcpy(frame+26,iv,8);
    assert(wifi_ccmp_crypt(&key,frame,26,16,true)==-EBADMSG);
    return 0;
}
"""
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "test.c"
            binary = Path(directory) / "test"
            source.write_text(prefix + CCMP + vectors)
            subprocess.run([os.environ["WIFI_RX_NATIVE_CC"], "-std=c11", "-Wall",
                            "-Wextra", "-Werror", str(source), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

    @unittest.skipUnless(os.environ.get("WIFI_RX_NATIVE_CC"), "requires pinned compiler")
    def test_actual_cache_selection_across_initial_jiffies_wrap(self):
        fragment = NET.split("\tfor (i = 0; i < WIFI_BSS_CACHE; i++) {", 1)[1]
        fragment = "\tfor (i = 0; i < WIFI_BSS_CACHE; i++) {" + fragment.split(
            "\tentry = cfg80211_inform_bss_data", 1)[0]
        prefix = r"""
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>
typedef uint8_t u8;
#define WIFI_BSS_CACHE 32
#define time_before(a,b) ((int32_t)((uint32_t)(a)-(uint32_t)(b))<0)
#define min_t(t,a,b) ((t)(a)<(t)(b)?(t)(a):(t)(b))
struct slot { u8 data[884]; uint32_t seen; bool valid; };
struct h432b_wifi_net { struct slot cache[32]; };
static uint32_t jiffies;
static bool ether_addr_equal(const u8 *a,const u8 *b) { return !memcmp(a,b,6); }
static void report(struct h432b_wifi_net *net,const u8 *bss,unsigned int length) {
    unsigned int slot=0,i;
"""
        vectors = r"""
}
int main(void) {
    struct h432b_wifi_net net={0};
    u8 bss[884]={0};
    jiffies=0xffff0000U;
    for (unsigned int n=1;n<=32;n++) { bss[4]=n; report(&net,bss,sizeof(bss)); jiffies++; }
    for (unsigned int n=1;n<=32;n++) {
        unsigned int count=0;
        for (unsigned int i=0;i<32;i++) count+=net.cache[i].valid&&net.cache[i].data[4]==n;
        assert(count==1);
    }
    jiffies=0;
    bss[4]=16; report(&net,bss,sizeof(bss)); /* zero is a valid timestamp */
    unsigned int valid=0;
    for (unsigned int i=0;i<32;i++) valid+=net.cache[i].valid;
    assert(valid==32);
    jiffies=10;
    bss[4]=33; report(&net,bss,sizeof(bss));
    for (unsigned int i=0;i<32;i++) assert(net.cache[i].data[4]!=1); /* oldest replaced */
    return 0;
}
"""
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "test.c"
            binary = Path(directory) / "test"
            source.write_text(prefix + fragment + vectors)
            subprocess.run([os.environ["WIFI_RX_NATIVE_CC"], "-std=c11", "-Wall",
                            "-Wextra", "-Werror", str(source), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

    def test_receive_service_is_not_starved_by_transmit(self):
        command = (FILES / "h432b-wifi-command.h").read_text()
        self.assertIn("unsigned int limit = 4", DATA)
        worker = DATA.split("static void wifi_net_tx_work", 1)[1]
        self.assertIn("wifi_command_drain", worker)
        self.assertIn("wifi_rx_drain", worker)
        self.assertIn("net->consecutive_empty = 0", worker)
        self.assertIn("max(WIFI_EVENT_MAX, WIFI_RX_MAX) + 512", command)
        self.assertIn("packet = data + max(WIFI_EVENT_MAX, WIFI_RX_MAX)", command)
        self.assertNotIn("if (i == 64)\\n\\t\\terror = -EOVERFLOW", NET)


if __name__ == "__main__":
    unittest.main()

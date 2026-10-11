# SPDX-License-Identifier: MIT
"""Malformed individual survey reports must not terminate the C2H stream."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

FILES = Path(__file__).resolve().parents[1] / "recipes-kernel/linux/files"


class WifiBss(unittest.TestCase):
    def test_native_record_admission_and_allocation_failure(self):
        cc = os.environ.get("WIFI_RX_NATIVE_CC")
        if not cc:
            self.skipTest("pinned-container native compiler not configured")
        source = (FILES / "h432b-wifi-net.h").read_text()
        source = source[source.index("static int wifi_net_report("):
                        source.index("static void wifi_net_finish_scan(")]
        harness = r'''
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define WIFI_BSS_CACHE 32
#define IEEE80211_CHAN_DISABLED 1
#define CFG80211_BSS_FTYPE_BEACON 1
#define GFP_KERNEL 0
#define min_t(t,a,b) ((t)(a) < (t)(b) ? (t)(a) : (t)(b))
#define time_before(a,b) ((a) < (b))
static unsigned long jiffies = 10;
struct channel { unsigned int flags; };
struct cfg80211_inform_bss { struct channel *chan; u64 boottime_ns; };
struct cfg80211_bss { int unused; };
struct h432b_wifi_net {
 void *wiphy;
 struct channel channels[13];
 struct { u8 data[884]; unsigned long seen; bool valid; } cache[32];
 unsigned int reports, scan_reports_dropped;
};
static bool fail_alloc;
static unsigned int informed;
static u16 get_unaligned_le16(const u8 *p) { return p[0] | p[1] << 8; }
static u32 get_unaligned_le32(const u8 *p) {
 return p[0] | p[1] << 8 | p[2] << 16 | (u32)p[3] << 24;
}
static u64 get_unaligned_le64(const u8 *p) {
 return get_unaligned_le32(p) | (u64)get_unaligned_le32(p + 4) << 32;
}
static bool is_valid_ether_addr(const u8 *p) {
 static const u8 zero[6]; return !(p[0] & 1) && memcmp(p, zero, 6);
}
static bool ether_addr_equal(const u8 *a, const u8 *b) { return !memcmp(a,b,6); }
static u64 ktime_get_boottime_ns(void) { return 1; }
static struct cfg80211_bss *cfg80211_inform_bss_data(void *w,
 const struct cfg80211_inform_bss *info, int type, const u8 *bssid,
 u64 timestamp, u16 capability, u16 interval, const u8 *ies,
 unsigned int length, int gfp) {
 static struct cfg80211_bss bss;
 (void)w; (void)info; (void)type; (void)bssid; (void)timestamp;
 (void)capability; (void)interval; (void)ies; (void)length; (void)gfp;
 informed++; return fail_alloc ? NULL : &bss;
}
static void cfg80211_put_bss(void *w, struct cfg80211_bss *b) { (void)w; assert(b); }
''' + source + r'''
static void valid(u8 *p) {
 memset(p, 0, 900); p[4] = 2; p[72] = 1; p[112] = 12;
}
int main(void) {
 struct h432b_wifi_net n = {0};
 u8 p[900];
 for (unsigned int i = 0; i < 128; i++) {
  valid(p); assert(wifi_net_report(&n,p,i) == 0);
 }
 assert(n.scan_reports_dropped == 128 && !informed);
 valid(p); assert(wifi_net_report(&n,p,885) == 0);
 valid(p); p[4] = 1; assert(wifi_net_report(&n,p,128) == 0);
 valid(p); p[112] = 11; assert(wifi_net_report(&n,p,128) == 0);
 valid(p); p[112] = 13; assert(wifi_net_report(&n,p,128) == 0);
 valid(p); p[72] = 14; assert(wifi_net_report(&n,p,128) == 0);
 valid(p); p[112] = 13; assert(wifi_net_report(&n,p,129) == 0);
 valid(p); p[112] = 14; p[129] = 1; assert(wifi_net_report(&n,p,130) == 0);
 assert(n.scan_reports_dropped == 135 && !informed);
 valid(p); n.channels[0].flags = IEEE80211_CHAN_DISABLED;
 assert(wifi_net_report(&n,p,128) == 0 && !informed);
 n.channels[0].flags = 0; fail_alloc = true;
 assert(wifi_net_report(&n,p,128) == 0 && n.scan_reports_dropped == 136);
 fail_alloc = false;
 assert(wifi_net_report(&n,p,128) == 0 && n.reports == 1);
 assert(informed == 2 && n.scan_reports_dropped == 136);
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            c = Path(directory) / "bss.c"
            exe = Path(directory) / "bss"
            c.write_text(harness)
            subprocess.run([cc, "-std=c11", "-Wall", "-Wextra", "-Werror",
                            "-fsanitize=undefined", str(c), "-o", str(exe)], check=True)
            subprocess.run([str(exe)], check=True)

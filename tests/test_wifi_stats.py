# SPDX-License-Identifier: MIT
"""Native ethtool statistics bounds and interface-down lifetime contracts."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

FILES = Path(__file__).resolve().parents[1] / "recipes-kernel/linux/files"


class WifiStats(unittest.TestCase):
    def test_native_snapshot_without_hardware_access(self):
        cc = os.environ.get("WIFI_RX_NATIVE_CC")
        if not cc:
            self.skipTest("pinned-container native compiler not configured")
        source = (FILES / "h432b-wifi-stats.h").read_text()
        source = source.replace("#include <linux/ethtool.h>", "")
        harness = r"""
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
#define BIT(n) (1U << (n))
#define READ_ONCE(x) (x)
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define ETH_GSTRING_LEN 32
#define ETH_SS_STATS 1
struct ethtool_stats { int unused; };
struct owner { int lock; struct { struct { unsigned int high_water; } rx; } command; };
struct h432b_wifi_net {
 struct owner *owner;
 struct { bool enabled; } reorder[16];
 u64 rx_ht_authenticated, rx_amsdu_authenticated, rx_addba_reports;
 u64 rx_reorder_dropped, rx_replay, rx_mic, rx_rejected, tx_failed;
 unsigned int recovery_attempts, recovery_successes, scan_reports_dropped;
 u16 tx_ba_requested;
 struct { bool ht, qos; } ht;
 u64 rx_ht_mcs[16];
};
struct net_device { struct h432b_wifi_net *private; };
static void *netdev_priv(struct net_device *dev) { return &dev->private; }
static void mutex_lock(int *lock) { assert(!(*lock)++); }
static void mutex_unlock(int *lock) { assert((*lock)-- == 1); }
static u32 ethtool_op_get_link(struct net_device *dev) { return 0; }
struct ethtool_ops {
 u32 (*get_link)(struct net_device *);
 int (*get_sset_count)(struct net_device *, int);
 void (*get_strings)(struct net_device *, u32, u8 *);
 void (*get_ethtool_stats)(struct net_device *, struct ethtool_stats *, u64 *);
};
""" + source + r"""
int main(void) {
 struct owner owner = {0};
 struct h432b_wifi_net net = { .owner = &owner };
 struct net_device dev = { &net };
 u64 output[34];
 u8 names[32 * ETH_GSTRING_LEN + 2];
 assert(wifi_net_ethtool_ops.get_sset_count(&dev, ETH_SS_STATS) == 32);
 assert(wifi_net_stat_count(&dev, 0) == -EOPNOTSUPP);
 memset(names, 0xcc, sizeof(names));
 wifi_net_stat_names(&dev, 0, names + 1);
 for (unsigned int i = 0; i < sizeof(names); i++) assert(names[i] == 0xcc);
 wifi_net_stat_names(&dev, ETH_SS_STATS, names + 1);
 assert(names[0] == 0xcc && names[sizeof(names) - 1] == 0xcc);
 assert(strcmp((char *)names + 1, "rx_ht_authenticated") == 0);
 assert(strcmp((char *)names + 1 + 31 * ETH_GSTRING_LEN, "rx_ht_mcs15") == 0);
 for (unsigned int i = 0; i < ARRAY_SIZE(output); i++) output[i] = UINT64_MAX;
 wifi_net_stats(&dev, NULL, output + 1);
 assert(output[0] == UINT64_MAX && output[33] == UINT64_MAX);
 for (unsigned int i = 1; i <= 32; i++) assert(output[i] == 0);
 net.rx_ht_authenticated = 100;
 net.rx_amsdu_authenticated = 200;
 net.rx_addba_reports = 300;
 net.rx_reorder_dropped = 400;
 net.rx_replay = 500; net.rx_mic = 600;
 net.rx_rejected = 700; net.tx_failed = 800;
 net.recovery_attempts = 2; net.recovery_successes = 1;
 net.reorder[0].enabled = net.reorder[15].enabled = true;
 net.tx_ba_requested = 0x202;
 net.ht.ht = net.ht.qos = true;
 owner.command.rx.high_water = 256;
 net.scan_reports_dropped = 42;
 for (unsigned int i = 0; i < 16; i++) net.rx_ht_mcs[i] = UINT64_C(1) << (i + 32);
 wifi_net_stats(&dev, NULL, output + 1);
 assert(output[1] == 100 && output[2] == 200 && output[3] == 300);
 assert(output[4] == 400 && output[5] == 500 && output[6] == 600);
 assert(output[7] == 700 && output[8] == 800);
 assert(output[9] == 2 && output[10] == 1 && output[11] == 0x8001);
 assert(output[12] == 0x202 && output[13] == 1 && output[14] == 1);
 assert(output[15] == 131072);
 assert(output[16] == 42);
 for (unsigned int i = 0; i < 16; i++) assert(output[17 + i] == (UINT64_C(1) << (i + 32)));
 assert(!owner.lock && output[0] == UINT64_MAX && output[33] == UINT64_MAX);
 return 0;
}
"""
        with tempfile.TemporaryDirectory() as folder:
            c = Path(folder) / "stats.c"
            exe = Path(folder) / "stats"
            c.write_text(harness)
            subprocess.run([cc, "-std=c11", "-Wall", "-Werror",
                            "-fsanitize=undefined", str(c), "-o", str(exe)], check=True)
            subprocess.run([str(exe)], check=True)

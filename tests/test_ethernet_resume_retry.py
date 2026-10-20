# SPDX-License-Identifier: MIT
"""Execute the patched resume branch with a failed first ndo_open."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

PATCH = Path(__file__).resolve().parents[1] / (
    "recipes-kernel/linux/files/0024-h432b-ethernet-full-resume.patch")


class EthernetResumeRetry(unittest.TestCase):
    def test_failed_open_remains_available_for_retry(self):
        cc = os.environ.get("BOOTSTATE_NATIVE_CC")
        if not cc:
            self.skipTest("pinned-container native compiler not configured")
        added = "\n".join(line[1:] for line in PATCH.read_text().splitlines()
                          if line.startswith("+") and not line.startswith("+++"))
        branch = added.rsplit("\tif (pdata->h432b_reopen) {", 1)[1].split(
            "\n\tif (netif_running(ndev))", 1)[0]
        harness = r"""
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <errno.h>
struct net_device { bool present, up; };
struct data { bool h432b_reopen; int reset_gpiod; };
static int locked, fail_once;
static void rtnl_lock(void) { assert(!locked++); }
static void rtnl_unlock(void) { assert(locked-- == 1); }
static void netif_device_attach(struct net_device *n) { n->present = true; }
static void netif_device_detach(struct net_device *n) { n->present = false; }
static int dev_open(struct net_device *n, void *unused) {
 assert(locked);
 if (!n->present) return -ENODEV;
 if (fail_once) { fail_once = 0; return -ENOMEM; }
 n->up = true;
 return 0;
}
#define gpiod_set_value_cansleep(...) ((void)0)
#define usleep_range(...) ((void)0)
#define msleep(...) ((void)0)
#define netdev_err(...) ((void)0)
static int resume(struct net_device *ndev, struct data *pdata) {
 int ret;
"""
        harness += "\tif (pdata->h432b_reopen) {" + branch + "\nreturn 0;\n}\n"
        harness += r"""
int main(void) {
 struct net_device net = { false, false };
 struct data data = { true, 1 };
 fail_once = 1;
 assert(resume(&net, &data) == -ENOMEM);
 assert(net.present && !net.up && !data.h432b_reopen && !locked);
 rtnl_lock();
 assert(dev_open(&net, 0) == 0 && net.up);
 rtnl_unlock();
 netif_device_detach(&net); net.up = false; data.h432b_reopen = true;
 assert(resume(&net, &data) == 0 && net.present && net.up);
 assert(!data.h432b_reopen && !locked);
 return 0;
}
"""
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "resume.c"
            binary = Path(directory) / "resume"
            source.write_text(harness)
            subprocess.run([cc, "-std=c11", "-Wall", "-Werror", str(source),
                            "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

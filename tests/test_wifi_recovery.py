# SPDX-License-Identifier: MIT
"""Fault-injection tests of the actual radio reset and recovery worker.

The MMC and scheduler doubles check ordering and bounded policy. They do not
replace module lifecycle, error recovery or suspend testing on the device.
"""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

FILES = Path(__file__).resolve().parents[1] / "recipes-kernel/linux/files"


def function(source, name):
    start = source.rfind("static ", 0, source.index(name + "("))
    opening = source.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end] + "\n"


PREFIX = r"""
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stddef.h>
#include <assert.h>
#include <errno.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
#define BIT(n) (1U << (n))
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define READ_ONCE(x) (x)
#define WRITE_ONCE(x, v) ((x) = (v))
"""


@unittest.skipUnless(os.environ.get("WIFI_RX_NATIVE_CC"),
                     "native C tests require the pinned container compiler")
class WifiRecovery(unittest.TestCase):
    def compile(self, source):
        with tempfile.TemporaryDirectory(prefix="wifi-recovery-") as directory:
            path = Path(directory) / "test.c"
            binary = Path(directory) / "test"
            path.write_text(PREFIX + source)
            subprocess.run([os.environ["WIFI_RX_NATIVE_CC"], "-std=c11", "-O2",
                            "-Wall", "-Wextra", "-Werror", "-fsanitize=undefined",
                            str(path), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

    def test_open_unwinds_partial_irq_claim_and_function_ownership(self):
        source = (FILES / "h432b-wifi-net.h").read_text()
        self.compile(r"""
#define MMC_CAP_SDIO_IRQ 1
#define SDIO_CCCR_IOEx 2
struct host { unsigned int caps; };
struct card { struct host *host; };
struct sdio_func { struct card *card; unsigned int cur_blksize, num; };
struct h432b_command_result { bool irq_owned, irq_native, cancelled; int cleanup; };
struct owner { int lock; struct h432b_command_result command; };
struct h432b_wifi_net {
 struct owner *owner; struct sdio_func *func;
 bool faulted, needs_restart, enabled_func, stopping;
 unsigned int saved_block_size; int last_error, workqueue, event_work;
};
struct net_device { struct h432b_wifi_net *private; };
static unsigned int calls, fail_at, restart_calls, enabled, irq, queued;
static int held, restart_error;
static void *netdev_priv(struct net_device *dev) { return &dev->private; }
static void mutex_lock(int *lock) { assert(!(*lock)++); }
static void mutex_unlock(int *lock) { assert((*lock)-- == 1); }
static void sdio_claim_host(struct sdio_func *func) { (void)func; assert(!held++); }
static void sdio_release_host(struct sdio_func *func) { (void)func; assert(held-- == 1); }
static int io(void) { assert(held); return ++calls == fail_at ? -EIO : 0; }
static int wifi_runtime_restart(struct h432b_wifi_net *net) {
 assert(net->owner->lock && !held); restart_calls++; return restart_error;
}
static u8 sdio_f0_readb(struct sdio_func *func, int off, int *error) {
 assert(off == SDIO_CCCR_IOEx); *error = io(); return enabled << func->num;
}
static int sdio_enable_func(struct sdio_func *func) {
 (void)func; int error = io(); if (!error) enabled = 1; return error;
}
static int sdio_disable_func(struct sdio_func *func) {
 (void)func; assert(held); enabled = 0; return 0;
}
static int sdio_set_block_size(struct sdio_func *func, unsigned int size) {
 int error = size == 512 ? io() : 0;
 if (!error) func->cur_blksize = size;
 return error;
}
static void h432b_wifi_irq(struct sdio_func *func) { (void)func; }
static int sdio_claim_irq(struct sdio_func *func, void (*handler)(struct sdio_func *)) {
 (void)func; assert(handler == h432b_wifi_irq); irq = 1; return io();
}
static int sdio_release_irq(struct sdio_func *func) {
 (void)func; assert(held); irq = 0; return 0;
}
static int wifi_command_arm(struct sdio_func *func, struct h432b_command_result *r) {
 (void)func; assert(r->irq_owned && irq); return io();
}
static void netif_carrier_off(struct net_device *dev) { (void)dev; }
static void netif_tx_disable(struct net_device *dev) { (void)dev; }
static void queue_delayed_work(int queue, int *work, int delay) {
 (void)queue; (void)work; assert(!delay); queued++;
}
""" + function(source, "wifi_net_start") + r"""
int main(void) {
 struct host host = { MMC_CAP_SDIO_IRQ };
 struct card card = { &host };
 struct sdio_func func = { &card, 64, 1 };
 struct owner owner = {0};
 struct h432b_wifi_net net;
 struct net_device dev = { &net };
 for (fail_at = 0; fail_at <= 5; fail_at++) {
  memset(&net, 0, sizeof(net)); memset(&owner, 0, sizeof(owner));
  net.owner = &owner; net.func = &func; net.stopping = true;
  func.cur_blksize = 64; calls = enabled = irq = queued = 0;
  int error = wifi_net_start(&dev);
  assert(!held && !owner.lock);
  if (fail_at) {
   assert(error == -EIO && net.faulted && net.stopping);
   assert(!enabled && !irq && !queued && !owner.command.irq_owned);
   assert(func.cur_blksize == 64);
  } else {
   assert(!error && !net.faulted && !net.stopping);
   assert(enabled && irq && queued == 1 && owner.command.irq_owned);
  }
 }
 fail_at = 0; calls = irq = enabled = queued = 0;
 memset(&owner, 0, sizeof(owner)); memset(&net, 0, sizeof(net));
 net.owner = &owner; net.func = &func; net.faulted = true;
 restart_error = -ETIMEDOUT;
 assert(wifi_net_start(&dev) == -ETIMEDOUT);
 assert(restart_calls == 1 && !calls && !held && !owner.lock);
 assert(net.faulted && net.stopping && !queued);
 restart_error = 0;
 assert(wifi_net_start(&dev) == 0 && restart_calls == 2);
 assert(!net.faulted && !net.needs_restart && !net.stopping && queued == 1);
 return 0;
}
""")

    def test_factory_sdio_shutdown_order_and_every_transfer_failure(self):
        power = (FILES / "h432b-wifi-power.h").read_text()
        ops = power[power.index("struct wifi_power_op {"):
                    power.index("static int wifi_signature(")]
        self.compile(r"""
struct sdio_func { int unused; };
struct h432b_power_result { unsigned int step; };
struct operation { unsigned int width, address, value; bool read; };
static struct operation log_ops[256];
static unsigned int calls, fail_at, reads, ready_at, sleeps, micro_delay;
static void transfer(unsigned int width, unsigned int address, unsigned int value,
                     bool read, int *error)
{
    assert(calls < ARRAY_SIZE(log_ops));
    log_ops[calls++] = (struct operation){width, address, value, read};
    *error = calls == fail_at ? -EIO : 0;
}
static u32 wifi_read(struct sdio_func *f, unsigned int width,
                     unsigned int offset, int *error)
{
    (void)f;
    u32 value = ++reads >= ready_at ? BIT(6) : 0;
    transfer(width, 0x8000 | offset, value, true, error);
    return value;
}
static void wifi_write(struct sdio_func *f, unsigned int width,
                       unsigned int offset, u32 value, int *error)
{
    (void)f;
    transfer(width, 0x8000 | offset, value, false, error);
}
static void sdio_writew(struct sdio_func *f, u16 value,
                        unsigned int address, int *error)
{
    (void)f;
    transfer(2, address, value, false, error);
}
static void usleep_range(unsigned int first, unsigned int last)
{
    assert(last >= first);
    if (first == 10000) sleeps++;
    else micro_delay += first;
}
static void clear(unsigned int failure, unsigned int ready)
{
    memset(log_ops, 0, sizeof(log_ops));
    calls = reads = sleeps = micro_delay = 0;
    fail_at = failure;
    ready_at = ready;
}
""" + ops + function(power, "wifi_power_off") + r"""
int main(void)
{
    struct sdio_func func = {};
    const struct operation expected[] = {
        {2, 0x09, 0, false}, {1, 0x801f, 0, false},
        {1, 0x8009, 0x38, false}, {1, 0x8009, 0x40, true},
        {1, 0x8003, 0x70, false}, {1, 0x8004, 0x06, false},
        {1, 0x8000, 0xff, false}, {1, 0x8001, 0xf6, false},
        {1, 0x8028, 0, false}, {1, 0x8020, 0x54, false},
        {1, 0x8003, 0x50, false}, {1, 0x8010, 0, false},
    };
    clear(0, 1);
    assert(wifi_power_off(&func) == 0);
    assert(calls == ARRAY_SIZE(expected) && micro_delay == 100 && sleeps == 0);
    for (unsigned int i = 0; i < calls; i++) {
        assert(log_ops[i].width == expected[i].width);
        assert(log_ops[i].address == expected[i].address);
        assert(log_ops[i].value == expected[i].value);
        assert(log_ops[i].read == expected[i].read);
    }
    for (unsigned int i = 1; i <= ARRAY_SIZE(expected); i++) {
        clear(i, 1);
        assert(wifi_power_off(&func) == -EIO && calls == i);
    }
    clear(0, 62);
    assert(wifi_power_off(&func) == -ETIMEDOUT);
    assert(reads == 61 && sleeps == 60 && calls == 64);
    clear(0, 61);
    assert(wifi_power_off(&func) == 0);
    assert(reads == 61 && sleeps == 60 && calls == 72);
    return 0;
}
""")

    def test_recovery_budget_rtnl_contention_remove_and_failure_policy(self):
        net = (FILES / "h432b-wifi-net.h").read_text()
        self.compile(r"""
#define WLAN_REASON_UNSPECIFIED 1
#define system_long_wq NULL
#define msecs_to_jiffies(v) (v)
#define container_of(p, type, member) ((type *)((char *)(p) - offsetof(type, member)))
#define to_delayed_work(p) ((struct delayed_work *)(p))
#define dev_info(...) ((void)0)
#define dev_err(...) ((void)0)
struct work_struct { int unused; };
struct delayed_work { struct work_struct work; };
struct net_device { bool running, attached; };
struct sdio_func { int dev; };
struct h432b_wifi_net {
    struct delayed_work recovery_work;
    struct net_device *dev;
    struct sdio_func *func;
    bool stopping, removing, faulted;
    unsigned int recovery_attempts, recovery_successes;
    int last_error;
};
static unsigned int queued, delay, stops, starts, disconnects, unlocks;
static bool rtnl_available;
static int start_error;
static struct h432b_wifi_net *active;
static void queue_delayed_work(void *q, struct delayed_work *w, unsigned int ms)
{
    (void)q;
    assert(w == &active->recovery_work);
    queued++;
    delay = ms;
}
static void wifi_net_link_down(struct h432b_wifi_net *net, unsigned int reason,
                               bool local)
{
    assert(net == active && reason == WLAN_REASON_UNSPECIFIED && local);
    disconnects++;
}
static bool rtnl_trylock(void) { return rtnl_available; }
static void rtnl_unlock(void) { unlocks++; }
static bool netif_running(struct net_device *dev) { return dev->running; }
static void netif_device_attach(struct net_device *dev) { dev->attached = true; }
static void netif_device_detach(struct net_device *dev) { dev->attached = false; }
static void wifi_net_stop(struct net_device *dev)
{
    assert(dev == active->dev && !dev->attached);
    active->stopping = true;
    stops++;
}
static int wifi_net_start(struct net_device *dev)
{
    assert(dev == active->dev && active->stopping);
    starts++;
    if (!start_error) { active->stopping = false; active->faulted = false; }
    return start_error;
}
""" + function(net, "wifi_net_recoverable") + function(net, "wifi_net_fault") +
            function(net, "wifi_net_recovery_work") + r"""
int main(void)
{
    struct net_device dev = {.running = true, .attached = true};
    struct sdio_func func = {};
    struct h432b_wifi_net net = {.dev = &dev, .func = &func};
    active = &net;
    assert(!wifi_net_recoverable(-ENOMEM));
    assert(!wifi_net_recoverable(-ECANCELED));
    assert(!wifi_net_recoverable(-ENOSPC));
    assert(wifi_net_recoverable(-EIO));
    wifi_net_fault(&net, -EIO);
    assert(net.faulted && net.last_error == -EIO && queued == 1 && delay == 200);
    assert(disconnects == 1);
    wifi_net_recovery_work(&net.recovery_work.work);
    assert(queued == 2 && delay == 100 && starts == 0 && net.recovery_attempts == 0);
    rtnl_available = true;
    wifi_net_recovery_work(&net.recovery_work.work);
    assert(starts == 1 && stops == 1 && unlocks == 1 && dev.attached);
    assert(!net.faulted && net.recovery_successes == 1 && net.recovery_attempts == 1);
    wifi_net_recovery_work(&net.recovery_work.work);
    assert(starts == 1);
    wifi_net_fault(&net, -ETIMEDOUT);
    start_error = -ETIMEDOUT;
    wifi_net_recovery_work(&net.recovery_work.work);
    assert(starts == 2 && net.recovery_attempts == 2 && !dev.attached);
    assert(net.stopping && net.faulted && delay == 2000);
    unsigned int before = queued;
    wifi_net_recovery_work(&net.recovery_work.work);
    assert(starts == 3 && net.recovery_attempts == 3 && queued == before);
    wifi_net_fault(&net, -EIO);
    wifi_net_recovery_work(&net.recovery_work.work);
    assert(starts == 3 && queued == before);
    net.recovery_attempts = 0;
    net.stopping = false;
    start_error = -ENOMEM;
    wifi_net_recovery_work(&net.recovery_work.work);
    assert(starts == 4 && queued == before && net.last_error == -ENOMEM);
    net.removing = true;
    wifi_net_fault(&net, -EIO);
    wifi_net_recovery_work(&net.recovery_work.work);
    assert(starts == 4 && queued == before);
    net.removing = false;
    dev.running = false;
    wifi_net_recovery_work(&net.recovery_work.work);
    assert(starts == 4);
    return 0;
}
""")

    def test_runtime_scan_has_no_debug_reply_dependency_and_cancel_is_bounded(self):
        net = (FILES / "h432b-wifi-net.h").read_text()
        self.compile(r"""
#define msecs_to_jiffies(v) (v)
#define jiffies 100
#define WIFI_SCAN_PARAMETERS 84
#define IEEE80211_CHAN_DISABLED 1
#define IEEE80211_CHAN_NO_IR 2
struct sdio_func { int unused; };
struct rx { int unused; };
struct h432b_command_result {
    u8 survey_channels[32], survey_nchannels;
    bool scanning, survey_done, cancelled;
    unsigned int survey_events, survey_count, survey_runs, survey_total;
    struct rx rx;
};
struct h432b_wifi_sample { struct h432b_command_result command; };
struct h432b_wifi_net {
    struct h432b_wifi_sample *owner;
    struct sdio_func *func;
    u8 *event_buffer;
    bool needs_restart;
    unsigned int scan_plan;
    struct { u32 flags; } channels[13];
};
static unsigned int commands, waits, host_claims, drains, fail_command;
static int command_error, wait_error;
static bool cancel_on_wait;
static void put_unaligned_le32(u32 value, u8 *p)
{
    p[0] = value; p[1] = value >> 8; p[2] = value >> 16; p[3] = value >> 24;
}
static int wifi_scan_parameters(const unsigned int *plan, unsigned int pass, u8 *p)
{
    (void)plan;
    assert(pass == 0);
    memset(p, 0, 84);
    put_unaligned_le32(48, p + 4);
    p[50] = 1; p[51] = 1; p[52] = 6; p[53] = 11; p[83] = 3;
    return 0;
}
static void wifi_scan_restrict(u8 *p, u16 allowed, u16 may_probe)
{
    assert(allowed == 0x1fff && may_probe == 0x1fff);
    (void)p;
}
static void sdio_claim_host(struct sdio_func *f) { (void)f; assert(!host_claims++); }
static void sdio_release_host(struct sdio_func *f) { (void)f; assert(host_claims-- == 1); }
static int wifi_h2c_send(struct sdio_func *f, struct h432b_command_result *r,
                         u8 code, const void *p, unsigned int length)
{
    (void)f; (void)r;
    const u8 *bytes = p;
    assert(host_claims == 1);
    commands++;
    if (commands == 1) {
        assert(code == 17 && length == 1 && bytes[0] == 1);
    } else {
        assert(commands == 2 && code == 18 && length == 84);
        assert(bytes[0] == 0 && bytes[4] == 48 && bytes[50] == 1);
        assert(bytes[51] == 1 && bytes[52] == 6 && bytes[53] == 11 && bytes[83] == 3);
        for (unsigned int i = 8; i < 50; i++) assert(bytes[i] == 0);
    }
    return commands == fail_command ? command_error : 0;
}
static int wifi_command_drain(struct sdio_func *f, struct h432b_command_result *r, u8 *p)
{
    (void)f; (void)p;
    drains++;
    if (waits) { r->survey_done = true; r->survey_count = 7; }
    return waits ? 1 : 0;
}
static int wifi_rx_drain(struct sdio_func *f, struct rx *r, u8 *p)
{
    (void)f; (void)r; (void)p;
    return 0;
}
static int wifi_command_arm(struct sdio_func *f, struct h432b_command_result *r)
{
    (void)f;
    return r->cancelled ? -ECANCELED : 0;
}
static int wifi_command_wait(struct sdio_func *f, struct h432b_command_result *r,
                             unsigned long deadline)
{
    (void)f;
    assert(deadline == 15100 && ++waits == 1);
    if (cancel_on_wait) r->cancelled = true;
    return wait_error;
}
static void clear(struct h432b_wifi_net *net)
{
    memset(&net->owner->command, 0, sizeof(net->owner->command));
    net->owner->command.survey_channels[0] = 1;
    net->owner->command.survey_channels[1] = 6;
    net->owner->command.survey_channels[2] = 11;
    net->owner->command.survey_nchannels = 3;
    net->needs_restart = false;
    commands = waits = host_claims = drains = fail_command = 0;
    command_error = wait_error = 0;
    cancel_on_wait = false;
}
""" + function(net, "wifi_net_run_scan") + r"""
int main(void)
{
    struct h432b_wifi_sample owner = {};
    struct sdio_func func = {};
    struct h432b_wifi_net net = {.owner = &owner, .func = &func};
    clear(&net);
    assert(wifi_net_run_scan(&net, 0) == 0);
    assert(commands == 2 && waits == 1 && drains == 2 && !host_claims);
    assert(owner.command.survey_runs == 1 && owner.command.survey_total == 7);
    assert(!owner.command.scanning && !net.needs_restart);
    clear(&net);
    cancel_on_wait = true;
    assert(wifi_net_run_scan(&net, 0) == -ECANCELED);
    assert(net.needs_restart && !owner.command.scanning && !host_claims);
    clear(&net);
    wait_error = -ETIMEDOUT;
    assert(wifi_net_run_scan(&net, 0) == -ETIMEDOUT && waits == 1);
    assert(net.needs_restart && !host_claims);
    clear(&net);
    fail_command = 1; command_error = -ENOMEM;
    assert(wifi_net_run_scan(&net, 0) == -ENOMEM);
    assert(!net.needs_restart && commands == 1 && !waits && !host_claims);
    clear(&net);
    fail_command = 2; command_error = -EIO;
    assert(wifi_net_run_scan(&net, 0) == -EIO);
    assert(commands == 2 && !waits && !host_claims);
    return 0;
}
""")


if __name__ == "__main__":
    unittest.main()

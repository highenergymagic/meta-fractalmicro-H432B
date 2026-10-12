# SPDX-License-Identifier: MIT
"""Exercise cache/USB ownership and returning paths with the pinned compiler."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1] / "recipes-bsp/u-boot"
FILES = ROOT / "files"


class NandCache(unittest.TestCase):
    def test_integrity_and_scope(self):
        nand = (FILES / "nand/u2nand.c").read_text()
        self.assertIn("ubi_volume_read_bounded(volume", nand)
        self.assertIn("u2_fb_layout(", nand)
        self.assertNotIn("crc32(0,(void *)U2_FB_ADDRESS,actual)", nand)
        self.assertIn("!h432b_nand_cache_active()", nand)
        handoff = (FILES / "bootstate/u2bootstate.c").read_text()
        self.assertLess(handoff.index("h432b_nand_cache_leave();"),
                        handoff.index("get_timer(kernel_started)"))
        self.assertIn("u2cachedboot; u2 probe",
                      (FILES / "bootstate/ab-autoboot.patch").read_text())
        self.assertIn("file://nand/u2nandcache.c",
                      (ROOT / "u-boot-h432b-ab_2012.10.bb").read_text())

    def test_native_lifecycle(self):
        compiler = os.environ.get("BOOTSTATE_NATIVE_CC")
        if not compiler:
            self.skipTest("Pinned compiler required")
        prelude = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define H432B_CACHE_NATIVE_TEST 1
#define DECLARE_GLOBAL_DATA_PTR
#define CR_M 1UL
#define CR_C 4UL
#define CONFIG_U2_ALLOC_TOP 0x46e00000UL
#define CONFIG_SYS_SDRAM_BASE 0x40000000UL
#define CONFIG_SYS_SDRAM_SIZE 0x10000000UL
#define CMD_RET_USAGE -2
#define CMD_RET_FAILURE -1
#define U_BOOT_CMD(...)
typedef int cmd_tbl_t;
struct bank { unsigned long start, size; };
struct bd { struct bank bi_dram[1]; };
struct global { unsigned long tlb_addr; struct bd *bd; };
static struct bd board = {{{CONFIG_SYS_SDRAM_BASE, CONFIG_SYS_SDRAM_SIZE}}};
static struct global global = {CONFIG_U2_ALLOC_TOP - 0x10000, &board};
static struct global *gd = &global;
static unsigned long cr, ttbcr;
static int usb, start_error, stop_error, run_error, simulate_handoff, events;
static int dma_idle = 1;
int h432b_nand_cache_active(void);
void h432b_nand_cache_leave(void);
static unsigned long get_cr(void) { return cr; }
static void set_cr(unsigned long value) { cr = value; }
static unsigned long read_ttbcr(void) { return ttbcr; }
static void write_ttbcr(unsigned long value) { ttbcr = value; }
static void cache_sync(void) { }
static int cache_dma_idle(void) { return dma_idle; }
int u2probe_start(void) { assert(!(cr & CR_C)); events++; usb = !start_error; return start_error; }
int u2probe_stop(void) { assert(!(cr & CR_C)); events++; if (!stop_error) usb = 0; return stop_error; }
static void dcache_enable(void) {
    assert(!usb && !ttbcr && !(cr & ((1UL << 28) | (1UL << 29))));
    assert(cr & (1UL << 23)); events++; cr |= CR_M | CR_C;
}
static void dcache_disable(void) { assert(!usb); events++; cr &= ~(CR_M | CR_C); }
static void invalidate_dcache_all(void) { assert(!(cr & (CR_M | CR_C))); events++; }
static int run_command(const char *cmd, int flags) {
    assert(!strcmp(cmd, "u2bootstate boot") && !flags);
    assert(h432b_nand_cache_active() && !usb && (cr & CR_C));
    if (simulate_handoff) h432b_nand_cache_leave();
    return run_error;
}
'''
        main = r'''
int main(void) {
    unsigned int i;
    for (i = 0; i < 8; i++) {
        unsigned long original = (1UL << 12) | ((unsigned long)(i & 1) << 28) |
            ((unsigned long)(i & 2) << 28) | ((unsigned long)(i & 4) << 21);
        cr = original; ttbcr = 7; run_error = -3; events = 0;
        assert(do_u2cachedboot(NULL, 0, 1, NULL) == -3);
        assert(cr == original && ttbcr == 7 && !h432b_nand_cache_active());
        assert(events == 5 && !usb);
        h432b_nand_cache_leave(); assert(events == 5);
    }
    simulate_handoff = 1; run_error = 0; events = 0;
    assert(!do_u2cachedboot(NULL, 0, 1, NULL)); assert(events == 5);
    assert(!h432b_nand_cache_active() && !(cr & (CR_M | CR_C)));
    events = 0; assert(do_u2cachedboot(NULL, 0, 2, NULL) == CMD_RET_USAGE);
    cr |= CR_M; assert(do_u2cachedboot(NULL, 0, 1, NULL) == -1); cr &= ~CR_M;
    cr |= CR_C; assert(do_u2cachedboot(NULL, 0, 1, NULL) == -1); cr &= ~CR_C;
    gd->tlb_addr++; assert(do_u2cachedboot(NULL, 0, 1, NULL) == -1); gd->tlb_addr--;
    board.bi_dram[0].size--; assert(do_u2cachedboot(NULL, 0, 1, NULL) == -1); board.bi_dram[0].size++;
    assert(events == 0);
    start_error = 1; assert(do_u2cachedboot(NULL, 0, 1, NULL) == -1); assert(events == 1);
    start_error = 0; stop_error = 1; events = 0;
    assert(do_u2cachedboot(NULL, 0, 1, NULL) == -1); assert(events == 2);
    assert(!h432b_nand_cache_active() && !(cr & (CR_M | CR_C)));
    stop_error = 0; dma_idle = 0; events = 0;
    assert(do_u2cachedboot(NULL, 0, 1, NULL) == -1); assert(events == 2);
    assert(!h432b_nand_cache_active() && !(cr & (CR_M | CR_C)));
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder)
            (path / "asm").mkdir()
            for header in ("common.h", "command.h", "asm/system.h", "asm/io.h"):
                (path / header).write_text("")
            (path / "test.c").write_text(
                prelude + (FILES / "nand/u2nandcache.c").read_text() + main)
            subprocess.run([compiler, "-std=gnu99", "-Wall", "-Wextra", "-Werror",
                            "-Wno-unused-parameter", "-I" + str(path),
                            str(path / "test.c"), "-o", str(path / "test")], check=True)
            subprocess.run([str(path / "test")], check=True)

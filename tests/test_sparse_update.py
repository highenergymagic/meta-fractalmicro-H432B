# SPDX-License-Identifier: MIT
"""Run the actual board update-session code against a fault-injected UBI model."""
import ctypes as c
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_sparse_image import sparse, RAW, FILL, SKIP

ROOT = Path(__file__).resolve().parents[1] / 'recipes-bsp/u-boot/files'


class SparseUpdate(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        folder = Path(cls.tmp.name)
        additions = '\n'.join(line[1:] for line in
                              (ROOT / '0021-h432b-sparse-fastboot.patch').read_text().splitlines()
                              if line.startswith('+') and not line.startswith('+++'))
        code = additions.split('static struct {', 1)[1].split('\n\tstruct h432b_sparse sparse;', 1)[0]
        source = r'''
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "sparse-image.h"
typedef uint32_t u32;
typedef unsigned char u8;
typedef struct { unsigned int sum; } sha256_context;
struct ubi_volume { int updating, upd_marker, corrupted; u32 used_bytes; void *upd_buf; };
static struct ubi_volume volume;
static void *ubi_devices[1];
static u8 stored[65536];
static u32 received, requested;
static int failure, starts;
#define min_t(t, a, b) ((t)(a) < (t)(b) ? (t)(a) : (t)(b))
#define vfree free
#define schedule() ((void)0)
static void sha256_starts(sha256_context *ctx) { ctx->sum = 0; }
static void sha256_update(sha256_context *ctx, const u8 *p, u32 n) {
    while (n--) ctx->sum = ctx->sum * 33 + *p++;
}
static void sha256_finish(sha256_context *ctx, u8 *digest) {
    memset(digest, 0, 32); memcpy(digest, &ctx->sum, 4);
}
static int ubi_start_update(void *dev, struct ubi_volume *v, u32 size) {
    if (v->updating || size > sizeof(stored)) return -1;
    starts++; received = 0; requested = size;
    v->updating = v->upd_marker = 1; v->upd_buf = malloc(4096);
    return failure == 1 ? -1 : 0;
}
static int ubi_more_update_data(void *dev, struct ubi_volume *v, const u8 *p, u32 n) {
    if (failure == 2 || !v->updating || n > requested - received) return -1;
    memcpy(stored + received, p, n); received += n;
    if (received == requested) {
        v->used_bytes = received; v->updating = v->upd_marker = 0;
        v->corrupted = 0; free(v->upd_buf); v->upd_buf = NULL;
        return n;
    }
    return 0;
}
static int volume_digest(struct ubi_volume *v, u8 *digest) {
    sha256_context ctx; sha256_starts(&ctx);
    sha256_update(&ctx, stored, v->used_bytes); sha256_finish(&ctx, digest);
    if (failure == 3) digest[0] ^= 1;
    return 0;
}
static int h432b_extension_write_range(void *ctx, u32 offset, u32 bytes,
                                      unsigned int type, const u8 *data) { return -1; }
static struct {
''' + code + r'''
void reset_test(void) {
    sparse_update_abort(); memset(&volume, 0, sizeof(volume));
    memset(stored, 0, sizeof(stored)); failure = starts = received = 0;
}
int apply(const void *p, size_t n, int fault) {
    struct h432b_sparse view;
    (void)sparse_mmc_emit;
    failure = fault;
    if (h432b_sparse_walk(p, n, 200U << 20, &view, NULL, NULL)) return -10;
    if (h432b_sparse_next(&view, view.first ? sparse_update.cursor : 0,
                         sparse_update.bytes)) return -11;
    if (view.first && sparse_update.vol != &volume) return -12;
    return sparse_volume_write(&volume, p, n, &view);
}
unsigned int state(unsigned int which) {
    switch (which) {
    case 0: return volume.updating;
    case 1: return volume.upd_marker;
    case 2: return volume.used_bytes;
    case 3: return starts;
    case 4: return sparse_update.cursor;
    default: return received;
    }
}
int contents(const void *p, size_t n) { return memcmp(p, stored, n); }
'''
        (folder / 'update.c').write_text(source)
        library = folder / 'update.so'
        subprocess.run(['cc', '-std=c99', '-Wall', '-Wextra', '-Werror',
                        '-Wno-unused-parameter', '-shared', '-fPIC',
                        '-I', str(ROOT / 'bootstate'), str(folder / 'update.c'),
                        '-o', str(library)], check=True)
        cls.lib = c.CDLL(str(library))
        cls.lib.apply.argtypes = [c.c_char_p, c.c_size_t, c.c_int]
        cls.lib.contents.argtypes = [c.c_char_p, c.c_size_t]

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def setUp(self):
        self.lib.reset_test()
        self.first = sparse([(RAW, 1, b'a' * 4096), (SKIP, 2, b'')])
        self.last = sparse([(SKIP, 1, b''), (FILL, 2, b'bcde')])

    def apply(self, payload, fault=0):
        return self.lib.apply(payload, len(payload), fault)

    def test_complete_static_update_and_readback(self):
        self.assertEqual(self.apply(self.first), 1)
        self.assertEqual([self.lib.state(i) for i in range(5)], [1, 1, 0, 1, 4096])
        self.assertEqual(self.apply(self.last), 0)
        self.assertEqual([self.lib.state(i) for i in range(5)], [0, 0, 12288, 1, 0])
        expected = b'a' * 4096 + b'bcde' * 2048
        self.assertEqual(self.lib.contents(expected, len(expected)), 0)

    def test_failures_retain_marker_and_restart_without_leaking_session(self):
        for fault in (1, 2):
            self.lib.reset_test()
            self.assertLess(self.apply(self.first, fault), 0)
            self.assertEqual(self.lib.state(0), 0)
            self.assertEqual(self.lib.state(1), 1)
            self.assertEqual(self.lib.state(4), 0)
            self.assertLess(self.apply(self.last), 0)
            self.assertEqual(self.apply(self.first), 1)
            self.assertEqual(self.apply(self.last), 0)
        self.lib.reset_test()
        self.assertEqual(self.apply(self.first), 1)
        self.assertLess(self.apply(self.last, 3), 0)
        self.assertEqual(self.lib.state(4), 0)

    def test_prefix_restart_and_missing_or_repeated_suffix(self):
        self.assertLess(self.apply(self.last), 0)
        self.assertEqual(self.lib.state(3), 0)
        self.assertEqual(self.apply(self.first), 1)
        self.assertEqual(self.apply(self.first), 1)
        self.assertEqual(self.lib.state(3), 2)
        self.assertEqual(self.apply(self.last), 0)
        self.assertLess(self.apply(self.last), 0)


if __name__ == '__main__':
    unittest.main()

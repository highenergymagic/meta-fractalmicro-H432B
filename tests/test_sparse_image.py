# SPDX-License-Identifier: MIT
"""Exercise the target sparse decoder with independent binary vectors."""
import ctypes as c
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

HEADER = Path(__file__).resolve().parents[1] / 'recipes-bsp/u-boot/files/bootstate'
RAW, FILL, SKIP = 0xcac1, 0xcac2, 0xcac3


def sparse(chunks, total=None):
    blocks = sum(count for kind, count, payload in chunks)
    return struct.pack('<I4H4I', 0xed26ff3a, 1, 0, 28, 12, 4096,
                       blocks if total is None else total, len(chunks), 0) + b''.join(
        struct.pack('<2H2I', kind, 0, count, 12 + len(payload)) + payload
        for kind, count, payload in chunks)


class SparseImage(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        root = Path(cls.tmp.name)
        source = root / 'sparse.c'
        source.write_text('''#include "sparse-image.h"
int run(const void *p, size_t n, uint32_t limit, h432b_sparse_emit emit,
        uint32_t cursor, uint32_t total, int sequential) {
    struct h432b_sparse view;
    if (h432b_sparse_walk(p, n, limit, &view, NULL, NULL)) return -1;
    if (sequential && h432b_sparse_next(&view, cursor, total)) return -2;
    return h432b_sparse_walk(p, n, limit, &view, emit, NULL);
}
''')
        library = root / 'sparse.so'
        subprocess.run(['cc', '-std=c99', '-Wall', '-Wextra', '-Werror',
                        '-shared', '-fPIC', '-I', str(HEADER), str(source),
                        '-o', str(library)], check=True)
        cls.lib = c.CDLL(str(library))
        cls.emit = c.CFUNCTYPE(c.c_int, c.c_void_p, c.c_uint32, c.c_uint32,
                              c.c_uint, c.c_void_p)
        cls.lib.run.argtypes = [c.c_char_p, c.c_size_t, c.c_uint32, cls.emit,
                                c.c_uint32, c.c_uint32, c.c_int]

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def run_image(self, payload, *, limit=2 << 30, cursor=0, total=0,
                  sequential=False, fail=False):
        self.writes = []
        def emit(context, offset, size, kind, data):
            self.writes.append((offset, size, kind, c.string_at(
                data, size if kind == RAW else 4)))
            return -1 if fail else 0
        return self.lib.run(payload, len(payload), limit, self.emit(emit),
                            cursor, total, sequential)

    def test_raw_fill_and_skip_offsets(self):
        data = sparse([(RAW, 1, b'x' * 4096), (SKIP, 8, b''),
                       (FILL, 2, b'abcd')])
        self.assertEqual(self.run_image(data), 0)
        self.assertEqual(self.writes, [(0, 4096, RAW, b'x' * 4096),
                                       (9 * 4096, 8192, FILL, b'abcd')])

    def test_two_gib_without_large_allocation(self):
        data = sparse([(FILL, 524288, b'abcd')])
        self.assertEqual(self.run_image(data), 0)
        self.assertEqual(self.writes[0][:3], (0, 2 << 30, FILL))
        self.assertNotEqual(self.run_image(data, limit=200 << 20), 0)
        self.assertFalse(self.writes)
        self.assertNotEqual(self.run_image(sparse([(FILL, 524289, b'abcd')])), 0)

    def test_android_resparse_sequence_and_readback_reconstruction(self):
        image = bytearray(b'?' * 4 * 4096)
        fragments = [sparse([(RAW, 2, b'a' * 8192), (SKIP, 2, b'')]),
                     sparse([(SKIP, 2, b''), (FILL, 2, b'bcde')])]
        for cursor, fragment in zip((0, 8192), fragments):
            self.assertEqual(self.run_image(fragment, cursor=cursor,
                                           total=len(image), sequential=True), 0)
            for offset, length, kind, data in self.writes:
                image[offset:offset + length] = data if kind == RAW else data * (length // 4)
        self.assertEqual(image, b'a' * 8192 + b'bcde' * 2048)
        for cursor in (0, 4096, 12288):
            self.assertNotEqual(self.run_image(fragments[1], cursor=cursor,
                                              total=len(image), sequential=True), 0)
            self.assertFalse(self.writes)
        self.assertNotEqual(self.run_image(fragments[1], cursor=8192,
                                          total=123, sequential=True), 0)

    def test_internal_holes_rejected_for_static_ubi(self):
        data = sparse([(RAW, 1, b'a' * 4096), (SKIP, 2, b''), (FILL, 1, b'abcd')])
        self.assertNotEqual(self.run_image(data, sequential=True), 0)
        self.assertFalse(self.writes)

    def test_every_truncation_precedes_any_write(self):
        data = sparse([(RAW, 1, bytes(4096)), (FILL, 9, b'abcd')])
        for size in range(len(data)):
            self.assertNotEqual(self.run_image(data[:size]), 0, size)
            self.assertFalse(self.writes, size)

    def test_malformed_fields_and_trailing_bytes(self):
        good = sparse([(RAW, 1, bytes(4096)), (FILL, 2, b'abcd')])
        cases = [good + b'junk', sparse([(SKIP, 4, b'')]),
                 sparse([(RAW, 1, bytes(4095))]), sparse([(FILL, 1, b'123')]),
                 sparse([(SKIP, 1, b'1234')]), sparse([(0xcac4, 1, b'1234')]),
                 sparse([(FILL, 0, b'1234')]), sparse([(FILL, 1, b'1234')], 2)]
        for offset, fmt, value in [(0, 'I', 0), (4, 'H', 2), (6, 'H', 1),
                                   (8, 'H', 27), (8, 'H', 65535), (10, 'H', 11),
                                   (12, 'I', 0), (12, 'I', 512), (16, 'I', 0xffffffff),
                                   (20, 'I', 0xffffffff), (24, 'I', 1),
                                   (32, 'I', 0xffffffff), (36, 'I', 0xffffffff)]:
            bad = bytearray(good)
            struct.pack_into('<' + fmt, bad, offset, value)
            cases.append(bytes(bad))
        for data in cases:
            self.assertNotEqual(self.run_image(data), 0)
            self.assertFalse(self.writes)

    def test_callback_error_propagates(self):
        data = sparse([(FILL, 1, b'abcd'), (RAW, 1, bytes(4096))])
        self.assertNotEqual(self.run_image(data, fail=True), 0)
        self.assertEqual(len(self.writes), 1)

    def test_extended_headers_are_consumed_once(self):
        data = bytearray(sparse([(FILL, 1, b'abcd')]))
        struct.pack_into('<H', data, 8, 32)
        struct.pack_into('<H', data, 10, 16)
        struct.pack_into('<I', data, 36, 20)
        data = data[:28] + b'extr' + data[28:40] + b'extr' + data[40:]
        self.assertEqual(self.run_image(bytes(data)), 0)
        self.assertEqual(self.writes, [(0, 4096, FILL, b'abcd')])


if __name__ == '__main__':
    unittest.main()

# SPDX-License-Identifier: MIT
"""Compile the target parser/verifier and exercise it with independent vectors."""
import ctypes as c
import hashlib
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / 'recipes-bsp/u-boot/files/bootstate'


class SlotPair(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not shutil.which('cc'):
            raise unittest.SkipTest('native C compiler unavailable')
        cls.tmp = tempfile.TemporaryDirectory()
        folder = Path(cls.tmp.name)
        source = folder / 'pair.c'
        source.write_text('''#include "slot-pair.h"
int check(const unsigned char *image, size_t size, const unsigned char *text,
          size_t len, void (*hash)(const unsigned char *, const unsigned char *, unsigned char *)) {
    struct h432b_verity v;
    if (h432b_verity_parse(text, len, &v)) return -1;
    return h432b_verity_verify(image, size, &v, hash);
}
int stream(const unsigned char *text, size_t len,
           int (*read)(void *, unsigned int, unsigned char *),
           void (*hash)(const unsigned char *, const unsigned char *, unsigned char *)) {
    struct h432b_verity v;
    unsigned char workspace[8192];
    if (h432b_verity_parse_limit(text, len, &v, 0x80000000U)) return -1;
    return h432b_verity_stream(&v, NULL, read, hash, workspace);
}
int parse(const unsigned char *text, size_t len, unsigned int limit) {
    struct h432b_verity v;
    return h432b_verity_parse_limit(text, len, &v, limit);
}
''')
        library = folder / 'pair.so'
        subprocess.run(['cc', '-std=c99', '-Wall', '-Wextra', '-Werror',
                        '-shared', '-fPIC', '-I', str(HEADER), str(source),
                        '-o', str(library)], check=True)
        cls.lib = c.CDLL(str(library))
        cls.callback_type = c.CFUNCTYPE(None, c.c_void_p, c.c_void_p, c.c_void_p)
        cls.lib.check.argtypes = [c.c_char_p, c.c_size_t, c.c_char_p, c.c_size_t,
                                 cls.callback_type]
        cls.read_type = c.CFUNCTYPE(c.c_int, c.c_void_p, c.c_uint, c.c_void_p)
        cls.lib.stream.argtypes = [c.c_char_p, c.c_size_t, cls.read_type, cls.callback_type]
        cls.lib.parse.argtypes = [c.c_char_p, c.c_size_t, c.c_uint]

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    @staticmethod
    def vector(blocks):
        salt = bytes(range(32))
        data = b''.join(bytes([i % 251]) * 4096 for i in range(blocks))
        level, tree = data, []
        while True:
            hashes = b''.join(hashlib.sha256(salt + level[i:i + 4096]).digest()
                              for i in range(0, len(level), 4096))
            level = hashes + bytes((-len(hashes)) % 4096)
            tree.insert(0, level)
            if len(level) == 4096:
                break
        root = hashlib.sha256(salt + level).hexdigest()
        image = data + b''.join(tree)
        descriptor = f'1 {blocks} {blocks} {root} {salt.hex()} {len(image)}\n'.encode()
        return image, descriptor

    def check(self, image, descriptor):
        def sha(salt, block, out):
            value = hashlib.sha256(c.string_at(salt, 32) + c.string_at(block, 4096)).digest()
            c.memmove(out, value, 32)
        return self.lib.check(image, len(image), descriptor, len(descriptor),
                              self.callback_type(sha))

    def test_tree_levels_and_corruption(self):
        for blocks in (2, 128, 129, 16400):
            image, desc = self.vector(blocks)
            self.assertEqual(self.check(image, desc), 0)
            for offset in (0, blocks * 4096 - 1, blocks * 4096, len(image) - 1):
                broken = bytearray(image)
                broken[offset] ^= 1
                self.assertNotEqual(self.check(bytes(broken), desc), 0)
            self.assertNotEqual(self.check(image[:-1], desc), 0)
            self.assertNotEqual(self.check(image + bytes(4096), desc), 0)
            self.assertNotEqual(self.check(image, desc.replace(b'1 ', b'2 ', 1)), 0)

    def test_descriptor_rejects_malformed_numbers_and_hashes(self):
        image, desc = self.vector(2)
        for bad in (b'', desc[:-1], desc + b'x', desc.replace(b'1 2 2', b'1 0 0'),
                    desc.replace(b'1 2 2', b'1 9999999999 2'),
                    desc.replace(b'1 2 2', b'1 2 3')):
            self.assertNotEqual(self.check(image, bad), 0)

    def test_streamed_extension_matches_independent_tree(self):
        def sha(salt, block, out):
            c.memmove(out, hashlib.sha256(c.string_at(salt, 32) +
                                         c.string_at(block, 4096)).digest(), 32)
        for blocks in (2, 128, 129, 16400):
            image, desc = self.vector(blocks)
            def run(payload, fail=None):
                def read(context, block, out):
                    if block == fail or (block + 1) * 4096 > len(payload):
                        return -1
                    c.memmove(out, payload[block * 4096:(block + 1) * 4096], 4096)
                    return 0
                return self.lib.stream(desc, len(desc), self.read_type(read), self.callback_type(sha))
            self.assertEqual(run(image), 0)
            self.assertNotEqual(run(image, blocks), 0)
            self.assertNotEqual(run(image[:-4096]), 0)
            for offset in (0, blocks * 4096, len(image) - 1):
                broken = bytearray(image)
                broken[offset] ^= 1
                self.assertNotEqual(run(bytes(broken)), 0)

    def test_extension_limit_does_not_expand_nand_contract(self):
        _, desc = self.vector(2)
        big = desc.replace(b'1 2 2', b'1 520000 520000').replace(b'12288\n', b'2147483648\n')
        self.assertEqual(self.lib.parse(big, len(big), 0x80000000), 0)
        self.assertNotEqual(self.lib.parse(big, len(big), 200 << 20), 0)
        for value in (b'2147483649', b'4294967296', b'18446744073709551616'):
            bad = big.replace(b'2147483648', value)
            self.assertNotEqual(self.lib.parse(bad, len(bad), 0x80000000), 0)


if __name__ == '__main__':
    unittest.main()

/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdio.h>
#include "bootstate.h"
#include "nand-write-boundary.h"
struct media {
    unsigned char data[2][H432B_ENV_BYTES];
    int valid[2], cut, fail_readback, failed_after_commit;
    unsigned writes, destination;
};
static int read_copy(void *ctx, unsigned copy, unsigned char *out)
{
    struct media *m = ctx;
    assert(copy < 2);
    if (!m->valid[copy] || (m->fail_readback && m->writes && copy == m->destination))
        return -1;
    memcpy(out, m->data[copy], H432B_ENV_BYTES);
    return 0;
}
static int replace_copy(void *ctx, unsigned copy, const unsigned char *in)
{
    struct media *m = ctx;
    unsigned count = m->cut < 0 ? H432B_ENV_BYTES : (unsigned)m->cut;
    assert(copy < 2 && count <= H432B_ENV_BYTES);
    m->writes++;
    m->destination = copy;
    /* UBI update marker invalidates the target until completion. */
    m->valid[copy] = 0;
    memcpy(m->data[copy], in, count);
    if (count != H432B_ENV_BYTES) return -1;
    m->valid[copy] = 1;
    return m->failed_after_commit ? -1 : 0;
}
static void init(struct media *m, unsigned serial)
{
    struct h432b_bootstate state = {0, {0, 1}, {3, 3}};
    memset(m, 0, sizeof(*m));
    m->cut = -1;
    state.serial = serial;
    assert(!h432b_env_encode(&state, m->data[0]));
    m->valid[0] = 1;
}
static void fix_crc(unsigned char *env)
{
    unsigned crc = h432b_env_crc(env + 5, H432B_ENV_DATA), i;
    for (i = 0; i < 4; i++) env[i] = crc >> (8 * i);
}
int main(void)
{
    struct media m;
    struct h432b_bootstore store = {&m, read_copy, replace_copy};
    struct h432b_bootstate state, other;
    unsigned char scratch[2 * H432B_ENV_BYTES], saved[H432B_ENV_BYTES];
    unsigned n, i;
    int chosen;
    assert(sizeof(unsigned) == 4);
    for (n = 0; n < 4096; n++) {
        unsigned long long off = (unsigned long long)n * 131072;
        assert(h432b_nand_write_allowed(off, 131072, 131072) ==
               (n >= 32 && n < 4088));
    }
    assert(!h432b_nand_write_allowed(0x1feff800ULL, 4096, 2048));
    assert(!h432b_nand_write_allowed(~0ULL, 2048, 2048));
    assert(!h432b_nand_write_allowed(0x400000, ~0ULL, 2048));
    assert(!h432b_nand_write_allowed(0x400001, 2048, 2048));
    assert(!h432b_nand_write_allowed(0x400000, 0, 2048));
    assert(h432b_env_crc((const unsigned char *)"123456789", 9) == 0xcbf43926U);
    for (n = 0; n < 256; n++) {
        init(&m, n);
        assert(h432b_bootstate_attempt(&store, scratch) == 0);
        assert(m.destination == 1);
        assert(h432b_bootstate_load(&store, scratch, &state) == 1);
        assert(state.left[0] == 2 && state.left[1] == 3);
        assert(state.serial == (unsigned char)(n + 1));
        assert(h432b_bootstate_attempt(&store, scratch) == 0);
        assert(h432b_bootstate_load(&store, scratch, &state) == 0);
        assert(state.left[0] == 1);
    }
    /* Every possible torn payload: old complete copy stays selected. */
    for (n = 0; n < H432B_ENV_BYTES; n++) {
        init(&m, 71);
        memcpy(saved, m.data[0], sizeof(saved));
        m.cut = n;
        assert(h432b_bootstate_attempt(&store, scratch) == -2);
        assert(!memcmp(saved, m.data[0], sizeof(saved)));
        assert(h432b_bootstate_load(&store, scratch, &state) == 0);
        assert(state.left[0] == 3 && state.serial == 71);
    }
    init(&m, 255); m.failed_after_commit = 1;
    assert(h432b_bootstate_attempt(&store, scratch) == -2);
    assert(h432b_bootstate_load(&store, scratch, &state) == 1);
    assert(state.serial == 0 && state.left[0] == 2);
    init(&m, 4); m.fail_readback = 1;
    assert(h432b_bootstate_attempt(&store, scratch) == -2);
    init(&m, 0);
    for (i = 0; i < 6; i++)
        assert(h432b_bootstate_attempt(&store, scratch) == (i < 3 ? 0 : 1));
    assert(h432b_bootstate_attempt(&store, scratch) == -1 && m.writes == 6);
    init(&m, 0); m.valid[0] = 0;
    assert(h432b_bootstate_attempt(&store, scratch) == -1 && !m.writes);
    init(&m, 9);
    assert(!h432b_env_decode(m.data[0], H432B_ENV_BYTES, &state));
    state.order[0] = 1; state.order[1] = 0;
    assert(!h432b_env_encode(&state, m.data[0]));
    assert(h432b_bootstate_attempt(&store, scratch) == 1);
    /* All single-bit payload corruption and truncated buffers are rejected. */
    memcpy(saved, m.data[0], sizeof(saved));
    for (n = 5; n < H432B_ENV_BYTES; n++) {
        memcpy(scratch, saved, sizeof(saved)); scratch[n] ^= 1;
        assert(h432b_env_decode(scratch, H432B_ENV_BYTES, &other));
    }
    for (n = 0; n < H432B_ENV_BYTES; n++)
        assert(h432b_env_decode(saved, n, &other));
    /* CRC-valid malformed state, duplicate keys and out-of-range attempts. */
    memset(scratch, 0, H432B_ENV_BYTES);
    {
        static const char invalid[] = "H432_BOOTSTATE_VERSION=1\0BOOT_ORDER=A B\0BOOT_A_LEFT=256\0BOOT_B_LEFT=3\0";
        memcpy(scratch + 5, invalid, sizeof(invalid));
    }
    fix_crc(scratch);
    assert(h432b_env_decode(scratch, H432B_ENV_BYTES, &other));
    memset(scratch, 0, H432B_ENV_BYTES);
    {
        static const char duplicate[] = "H432_BOOTSTATE_VERSION=1\0BOOT_ORDER=A B\0BOOT_A_LEFT=3\0BOOT_B_LEFT=3\0BOOT_A_LEFT=2\0";
        memcpy(scratch + 5, duplicate, sizeof(duplicate));
    }
    fix_crc(scratch);
    assert(h432b_env_decode(scratch, H432B_ENV_BYTES, &other));
    memset(scratch + 5, 'x', H432B_ENV_DATA); fix_crc(scratch);
    assert(h432b_env_decode(scratch, H432B_ENV_BYTES, &other));
    state.serial = 10; other = state;
    chosen = h432b_env_select(1, &state, 1, &other); assert(chosen == 0);
    assert(h432b_env_select(0, &state, 1, &other) == 1);
    state.serial = 42; state.left[0] = 2; state.left[1] = 0;
    assert(h432b_bootstate_can_mark_good(&state, 0, 42));
    assert(h432b_bootstate_can_mark_good(&state, 1, 42));
    assert(!h432b_bootstate_can_mark_good(&state, 2, 42));
    for (n = 0; n < 256; n++)
        assert(h432b_bootstate_can_mark_good(&state, 0, n) == (n == 42));
    state.left[0] = 3;
    assert(!h432b_bootstate_can_mark_good(&state, 0, 42));
    puts("BOOTSTATE_MODEL_PASS: format, wrap, torn updates, fallback, exhaustion");
    return 0;
}

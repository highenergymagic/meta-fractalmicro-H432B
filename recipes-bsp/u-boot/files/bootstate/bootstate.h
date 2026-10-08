/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef H432B_BOOTSTATE_H
#define H432B_BOOTSTATE_H
#ifndef H432B_BOOTSTATE_UBOOT
#include <stddef.h>
#include <string.h>
#endif

/* Standard little-endian redundant U-Boot environment: CRC32, serial, data.
 * This reader imports only boot-state fields, never executable environment.
 * UBI must report a complete, non-corrupt dynamic volume before decode.
 */
#define H432B_ENV_BYTES 4096U
#define H432B_ENV_HEADER 5U
#define H432B_ENV_DATA (H432B_ENV_BYTES - H432B_ENV_HEADER)
struct h432b_bootstate {
    unsigned char serial;
    unsigned char order[2]; /* 0=A, 1=B */
    unsigned char left[2];
};
struct h432b_bootstore {
    void *context;
    /* read must reject a volume with an update marker or wrong geometry. */
    int (*read)(void *, unsigned, unsigned char *);
    /* replace must update ONLY the requested redundant volume. */
    int (*replace)(void *, unsigned, const unsigned char *);
};

static unsigned h432b_env_crc(const unsigned char *p, size_t n)
{
    unsigned crc = 0xffffffffU, bit;
    while (n--) {
        crc ^= *p++;
        for (bit = 0; bit < 8; bit++)
            crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
    }
    return crc ^ 0xffffffffU;
}
static int h432b_decimal(const unsigned char *p, size_t n, unsigned char *out)
{
    unsigned value = 0;
    size_t i;
    if (!n || n > 3 || (n > 1 && p[0] == '0')) return -1;
    for (i = 0; i < n; i++) {
        if (p[i] < '0' || p[i] > '9') return -1;
        value = value * 10 + p[i] - '0';
    }
    if (value > 255) return -1;
    *out = value;
    return 0;
}
static int h432b_env_decode(const unsigned char *env, size_t size,
                            struct h432b_bootstate *out)
{
    size_t pos = H432B_ENV_HEADER, end, equal;
    unsigned crc, seen = 0, field;
    struct h432b_bootstate state;
    if (size != H432B_ENV_BYTES) return -1;
    crc = (unsigned)env[0] | (unsigned)env[1] << 8 |
          (unsigned)env[2] << 16 | (unsigned)env[3] << 24;
    if (crc != h432b_env_crc(env + H432B_ENV_HEADER, H432B_ENV_DATA))
        return -1;
    memset(&state, 0, sizeof(state));
    state.serial = env[4];
    while (pos < size && env[pos]) {
        end = pos;
        while (end < size && env[end]) end++;
        if (end == size) return -1;
        equal = pos;
        while (equal < end && env[equal] != '=') equal++;
        if (equal == pos || equal == end) return -1;
        field = 0;
        if (equal - pos == 22 && !memcmp(env + pos, "H432_BOOTSTATE_VERSION", 22)) {
            field = 1;
            if (end - equal != 2 || env[equal + 1] != '1') return -1;
        } else if (equal - pos == 10 && !memcmp(env + pos, "BOOT_ORDER", 10)) {
            field = 2;
            if (end - equal != 4 || env[equal + 2] != ' ' ||
                !((env[equal + 1] == 'A' && env[equal + 3] == 'B') ||
                  (env[equal + 1] == 'B' && env[equal + 3] == 'A'))) return -1;
            state.order[0] = env[equal + 1] - 'A';
            state.order[1] = env[equal + 3] - 'A';
        } else if (equal - pos == 11 &&
                   (!memcmp(env + pos, "BOOT_A_LEFT", 11) ||
                    !memcmp(env + pos, "BOOT_B_LEFT", 11))) {
            unsigned slot = env[pos + 5] - 'A';
            field = 4U << slot;
            if (h432b_decimal(env + equal + 1, end - equal - 1,
                              &state.left[slot])) return -1;
        }
        if (field && (seen & field)) return -1;
        seen |= field;
        pos = end + 1;
    }
    /* Each entry ended with NUL; this requires the additional final NUL. */
    if (pos >= size || seen != 15) return -1;
    *out = state;
    return 0;
}
static size_t h432b_append_number(unsigned char *p, unsigned n)
{
    size_t count = 0;
    if (n >= 100) p[count++] = '0' + n / 100;
    if (n >= 10) p[count++] = '0' + (n / 10) % 10;
    p[count++] = '0' + n % 10;
    return count;
}
static int h432b_env_encode(const struct h432b_bootstate *state,
                            unsigned char *env)
{
    static const char version[] = "H432_BOOTSTATE_VERSION=1";
    static const char order[] = "BOOT_ORDER=A B";
    static const char left[] = "BOOT_A_LEFT=";
    size_t pos = H432B_ENV_HEADER, start;
    unsigned slot, crc;
    if (state->order[0] > 1 || state->order[1] > 1 ||
        state->order[0] == state->order[1]) return -1;
    memset(env, 0, H432B_ENV_BYTES);
    env[4] = state->serial;
    memcpy(env + pos, version, sizeof(version)); pos += sizeof(version);
    memcpy(env + pos, order, sizeof(order));
    env[pos + 11] = 'A' + state->order[0];
    env[pos + 13] = 'A' + state->order[1];
    pos += sizeof(order);
    for (slot = 0; slot < 2; slot++) {
        start = pos;
        memcpy(env + pos, left, sizeof(left) - 1);
        env[start + 5] = 'A' + slot;
        pos += sizeof(left) - 1;
        pos += h432b_append_number(env + pos, state->left[slot]);
        pos++;
    }
    crc = h432b_env_crc(env + H432B_ENV_HEADER, H432B_ENV_DATA);
    for (slot = 0; slot < 4; slot++) env[slot] = crc >> (8 * slot);
    return 0;
}
/* Incremental flag selection agrees with U-Boot/libubootenv, including
 * 255->0 wrap and equal-flag preference for the first copy.
 */
static int h432b_env_select(int valid0, const struct h432b_bootstate *a,
                           int valid1, const struct h432b_bootstate *b)
{
    if (!valid0) return valid1 ? 1 : -1;
    if (!valid1) return 0;
    if (a->serial == 255 && b->serial == 0) return 1;
    if (b->serial == 255 && a->serial == 0) return 0;
    return b->serial > a->serial ? 1 : 0;
}
static int h432b_bootstate_load(const struct h432b_bootstore *store,
                               unsigned char *scratch,
                               struct h432b_bootstate *state)
{
    struct h432b_bootstate copies[2];
    int valid[2], selected;
    unsigned i;
    memset(copies, 0, sizeof(copies));
    for (i = 0; i < 2; i++)
        valid[i] = !store->read(store->context, i, scratch) &&
                   !h432b_env_decode(scratch, H432B_ENV_BYTES, &copies[i]);
    selected = h432b_env_select(valid[0], &copies[0], valid[1], &copies[1]);
    if (selected >= 0) *state = copies[selected];
    return selected;
}
/* A health report may acknowledge only its exact persisted boot attempt. */
static int h432b_bootstate_can_mark_good(const struct h432b_bootstate *state,
                                        unsigned slot, unsigned serial)
{
    return slot < 2 && serial == state->serial && state->left[slot] < 3;
}
/* Consumes a RAUC attempt before returning a bootable slot.
 * -1: no valid state/attempts; -2: persistence/readback failure.
 * On failure the caller must not boot the candidate. No implicit defaults.
 * scratch needs two environment-sized buffers.
 */
static int h432b_bootstate_attempt(const struct h432b_bootstore *store,
                                  unsigned char *scratch)
{
    struct h432b_bootstate state, check;
    int current, slot = -1;
    unsigned i, target;
    current = h432b_bootstate_load(store, scratch, &state);
    if (current < 0) return -1;
    for (i = 0; i < 2; i++)
        if (state.left[state.order[i]]) { slot = state.order[i]; break; }
    if (slot < 0) return -1;
    state.left[slot]--;
    state.serial++;
    target = (unsigned)current ^ 1U;
    if (h432b_env_encode(&state, scratch) ||
        store->replace(store->context, target, scratch) ||
        store->read(store->context, target, scratch + H432B_ENV_BYTES) ||
        memcmp(scratch, scratch + H432B_ENV_BYTES, H432B_ENV_BYTES) ||
        h432b_env_decode(scratch + H432B_ENV_BYTES, H432B_ENV_BYTES, &check))
        return -2;
    return slot;
}
#endif

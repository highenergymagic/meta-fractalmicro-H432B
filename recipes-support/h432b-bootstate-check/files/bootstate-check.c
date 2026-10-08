/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include "bootstate.h"
#include <libuboot.h>
static int read_copy(void *context, unsigned copy, unsigned char *buffer)
{
    char **paths = context;
    size_t total = 0;
    int fd = open(paths[copy], O_RDONLY | O_NOFOLLOW);
    if (fd < 0) return -1;
    while (total < H432B_ENV_BYTES) {
        ssize_t n = read(fd, buffer + total, H432B_ENV_BYTES - total);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) { close(fd); return -1; }
        total += n;
    }
    return close(fd);
}
static int mark_good(const char *slot_arg, const char *serial_arg)
{
    char *paths[] = {"/dev/ubi0_5", "/dev/ubi0_6"};
    struct h432b_bootstore store = {paths, read_copy, NULL};
    struct h432b_bootstate state;
    struct uboot_ctx *ctx = NULL;
    unsigned char buffer[H432B_ENV_BYTES], serial;
    char key[] = "BOOT_A_LEFT";
    char expected[4], order[4], *value;
    int ret = 1, slot, opened = 0;
    if (strlen(slot_arg) != 1 || (slot_arg[0] != 'A' && slot_arg[0] != 'B') ||
        h432b_decimal((const unsigned char *)serial_arg, strlen(serial_arg), &serial))
        return 2;
    slot = slot_arg[0] - 'A';
    key[5] = slot_arg[0];
    if (libuboot_initialize(&ctx, NULL) ||
        libuboot_read_config(ctx, "/etc/fw_env.config")) goto out;
    opened = 1;
    /* libuboot_open holds the same lock as fw_setenv until close. */
    if (libuboot_open(ctx) ||
        h432b_bootstate_load(&store, buffer, &state) < 0 ||
        !h432b_bootstate_can_mark_good(&state, slot, serial)) goto out;
    snprintf(expected, sizeof(expected), "%u", state.left[slot]);
    snprintf(order, sizeof(order), "%c %c", 'A' + state.order[0], 'A' + state.order[1]);
    value = libuboot_get_env(ctx, key);
    if (!value || strcmp(value, expected)) { free(value); goto out; }
    free(value);
    value = libuboot_get_env(ctx, "BOOT_ORDER");
    if (!value || strcmp(value, order)) { free(value); goto out; }
    free(value);
    if (libuboot_set_env(ctx, key, "3") || libuboot_env_store(ctx)) goto out;
    if (h432b_bootstate_load(&store, buffer, &state) < 0 ||
        state.serial != (unsigned char)(serial + 1) || state.left[slot] != 3)
        goto out;
    printf("Marked slot %c successful; attempts=3 serial=%u\n", slot_arg[0], state.serial);
    ret = 0;
out:
    if (opened) libuboot_close(ctx);
    if (ctx) libuboot_exit(ctx);
    if (ret) fprintf(stderr, "Boot success refused: stale attempt or invalid storage\n");
    return ret;
}
int main(int argc, char **argv)
{
    unsigned char buffer[H432B_ENV_BYTES];
    struct h432b_bootstate state;
    struct h432b_bootstore store;
    int copy;
    if (argc == 4 && !strcmp(argv[1], "--mark-good"))
        return mark_good(argv[2], argv[3]);
    if (argc == 2 && !strcmp(argv[1], "--emit-initial-b")) {
        /* Generate a file only. Installation is a separate explicit action. */
        memset(&state, 0, sizeof(state));
        state.order[0] = 1; state.order[1] = 0;
        state.left[1] = 3;
        if (h432b_env_encode(&state, buffer)) return 1;
        return fwrite(buffer, 1, sizeof(buffer), stdout) != sizeof(buffer);
    }
    if (argc != 4 || strcmp(argv[1], "--inspect")) {
        fprintf(stderr, "usage: %s --inspect COPY0 COPY1 | --emit-initial-b\n", argv[0]);
        return 2;
    }
    store.context = argv + 2;
    store.read = read_copy;
    store.replace = NULL;
    copy = h432b_bootstate_load(&store, buffer, &state);
    if (copy < 0) {
        fprintf(stderr, "No valid bootstate copies; no implicit defaults\n");
        return 1;
    }
    printf("copy=%d serial=%u BOOT_ORDER=%c %c BOOT_A_LEFT=%u BOOT_B_LEFT=%u\n",
           copy, state.serial, 'A' + state.order[0], 'A' + state.order[1],
           state.left[0], state.left[1]);
    return 0;
}

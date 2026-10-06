/* SPDX-License-Identifier: MIT */
/* Read-only GPH2[6] observation. No pinmux, pull, IRQ or GPIO writes. */
#define _POSIX_C_SOURCE 200809L
#define _FILE_OFFSET_BITS 64
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

static unsigned mux(uint32_t con) { return (con >> 24) & 15u; }
static unsigned level(uint32_t dat) { return (dat >> 6) & 1u; }
static double now(void) {
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t)) { perror("clock"); exit(1); }
    return t.tv_sec + t.tv_nsec / 1e9;
}
int main(int argc, char **argv) {
    if (argc == 2 && !strcmp(argv[1], "--self-test")) {
        if (mux(0x0f000000) != 15 || mux(0) != 0 ||
            level(0xbf) != 0 || level(0x40) != 1) return 1;
        puts("PASS: GPH2[6] mux/level decoding; no device opened");
        return 0;
    }
    char *end;
    if (argc != 2) { fprintf(stderr, "usage: read-power-input SECONDS (1..300)\n"); return 2; }
    errno = 0;
    long seconds = strtol(argv[1], &end, 10);
    if (errno || *end || seconds < 1 || seconds > 300) return 2;
    setvbuf(stdout, NULL, _IOLBF, 0);
    int fd = open("/dev/mem", O_RDONLY | O_SYNC);
    if (fd < 0) { perror("/dev/mem"); return 1; }
    void *mapping = mmap(NULL, 4096, PROT_READ, MAP_SHARED, fd, (off_t)0xe0200000);
    close(fd);
    if (mapping == MAP_FAILED) { perror("read-only mmap"); return 1; }
    volatile const uint32_t *gpio = mapping;
    uint32_t con = gpio[0xc40 / 4], dat = gpio[0xc44 / 4];
    uint32_t pud = gpio[0xc48 / 4];
    printf("GPH2 CON=%08x DAT=%08x PUD=%08x; pin6 mux=%u pull=%u\n",
           con, dat, pud, mux(con), (pud >> 12) & 3u);
    if (mux(con) != 0 && mux(con) != 15) {
        fprintf(stderr, "Refusing: pin6 is not input/EINT. Nothing changed.\n");
        munmap(mapping, 4096); return 1;
    }
    unsigned last = level(dat), changes = 0;
    double start = now(), heartbeat = start;
    printf("ARMED level=%u; %ld seconds; read-only, no shutdown action\n", last, seconds);
    const struct timespec interval = {0, 10000000};
    while (now() - start < seconds) {
        if (gpio[0xc40 / 4] != con || gpio[0xc48 / 4] != pud) {
            fprintf(stderr, "Mux/pull changed externally; stopping\n");
            munmap(mapping, 4096); return 1;
        }
        unsigned current = level(gpio[0xc44 / 4]);
        double t = now();
        if (current != last) {
            printf("CHANGE t=%.3f level=%u\n", t - start, current);
            last = current; changes++;
        }
        if (t - heartbeat >= 15) {
            printf("WAITING t=%.1f level=%u changes=%u\n", t-start, last, changes);
            heartbeat = t;
        }
        nanosleep(&interval, NULL);
    }
    printf("DONE changes=%u final=%u; no register writes\n", changes, last);
    munmap(mapping, 4096);
    return 0;
}

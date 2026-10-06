/* SPDX-License-Identifier: MIT */
/* Explicit experiment on INFORM7 only, never automatic or a NAND operation. */
#define _FILE_OFFSET_BITS 64
#define _POSIX_C_SOURCE 200809L
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#define TEST_MAGIC 0x48345254U
int main(int argc, char **argv)
{
    int arm;
    if (argc != 2) return 2;
    if (!strcmp(argv[1], "--arm-retention-test")) arm = 1;
    else if (!strcmp(argv[1], "--clear-retention-test")) arm = 0;
    else return 2;
    if (sysconf(_SC_PAGESIZE) != 4096) return 1;
    int fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0) { perror("/dev/mem"); return 1; }
    void *map = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                     MAP_SHARED, fd, (off_t)0xe010f000U);
    if (map == MAP_FAILED) { perror("mmap"); close(fd); return 1; }
    volatile uint32_t *reg = (volatile uint32_t *)((char *)map + 0x1c);
    uint32_t before = *reg;
    uint32_t expected = arm ? 0U : TEST_MAGIC;
    uint32_t after = arm ? TEST_MAGIC : 0U;
    int result = 1;
    if (before != expected) {
        fprintf(stderr, "INFORM7=%08x, expected %08x; no write\n", before, expected);
    } else {
        *reg = after;
        __sync_synchronize();
        uint32_t observed = *reg;
        printf("INFORM7: %08x -> %08x\n", before, observed);
        result = observed == after ? 0 : 1;
    }
    munmap(map, 4096);
    close(fd);
    return result;
}

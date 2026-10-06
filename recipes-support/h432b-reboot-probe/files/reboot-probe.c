/* SPDX-License-Identifier: MIT */
/* Inventory only. No selectable addresses and no register writes. */
#define _FILE_OFFSET_BITS 64
#define _POSIX_C_SOURCE 200809L
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    (void)argv;
    if (argc != 1) return 2;
    long page = sysconf(_SC_PAGESIZE);
    if (page != 4096) {
        fputs("Unexpected page size\n", stderr);
        return 1;
    }
    int fd = open("/dev/mem", O_RDONLY | O_SYNC);
    if (fd < 0) { perror("/dev/mem"); return 1; }
    const uint32_t bases[] = {0xe010a000U, 0xe010f000U};
    for (unsigned bank = 0; bank < 2; bank++) {
        void *map = mmap(NULL, 4096, PROT_READ, MAP_SHARED, fd, (off_t)bases[bank]);
        if (map == MAP_FAILED) { perror("mmap"); close(fd); return 1; }
        volatile const uint32_t *regs = map;
        for (unsigned i = 0; i < (bank ? 8U : 1U); i++) {
            uint32_t value = regs[i];
            printf("%s%u 0x%08x = 0x%08x\n",
                   bank ? "INFORM" : "RST_STAT", i, bases[bank] + 4*i, value);
            fflush(stdout);
        }
        if (munmap(map, 4096)) { perror("munmap"); close(fd); return 1; }
    }
    return close(fd) ? 1 : 0;
}

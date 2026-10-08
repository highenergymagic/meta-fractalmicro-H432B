/* SPDX-License-Identifier: MIT */
/* Read-only, fixed register allowlist. Never touch FIFO, EEPROM or CSR commands. */
#define _FILE_OFFSET_BITS 64
#define _POSIX_C_SOURCE 200809L
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

struct sample { uint32_t address; const char *name; };
static const struct sample host[] = {
    {0xe02002e0, "MP01CON"}, {0xe0100464, "CLK_GATE_IP1"},
    {0xe8000000, "SROM_BW"}, {0xe8000018, "SROM_BC5"},
    {0xe0200c20, "GPH1CON"}, {0xe0200c24, "GPH1DAT"},
};
static const struct sample chip[] = {
    {0xa8000064, "BYTE_TEST"}, {0xa8000050, "ID_REV"},
    {0xa8000054, "IRQ_CFG"}, {0xa800005c, "INT_EN"},
    {0xa8000074, "HW_CFG"}, {0xa8000084, "PMT_CTRL"},
};
int main(int argc, char **argv)
{
    const struct sample *regs;
    size_t count;
    if (argc != 2) {
        fprintf(stderr, "Usage: %s --host-bus|--controller\n", argv[0]);
        return 2;
    }
    if (!strcmp(argv[1], "--host-bus")) {
        regs = host; count = sizeof(host) / sizeof(host[0]);
    } else if (!strcmp(argv[1], "--controller")) {
        regs = chip; count = sizeof(chip) / sizeof(chip[0]);
    } else return 2;
    long page = sysconf(_SC_PAGESIZE);
    if (page <= 0 || (page & (page - 1))) return 1;
    int fd = open("/dev/mem", O_RDONLY | O_SYNC);
    if (fd < 0) { perror("/dev/mem"); return 1; }
    for (size_t i = 0; i < count; ++i) {
        uint32_t base = regs[i].address & ~((uint32_t)page - 1);
        void *map = mmap(NULL, (size_t)page, PROT_READ, MAP_SHARED, fd, (off_t)base);
        if (map == MAP_FAILED) { perror("mmap"); close(fd); return 1; }
        volatile const uint32_t *reg = (volatile const uint32_t *)
            ((const unsigned char *)map + (regs[i].address - base));
        uint32_t value;
        if (regs == chip) {
            /* CE bank5 configuration is a 16-bit bus; ordered halfword cycles. */
            volatile const uint16_t *half = (volatile const uint16_t *)reg;
            uint32_t low = half[0];
            uint32_t high = half[1];
            value = low | (high << 16);
        } else value = *reg;
        printf("%s 0x%08x = 0x%08x\n", regs[i].name, regs[i].address, value);
        fflush(stdout);
        munmap(map, (size_t)page);
        if (regs == chip && i == 0 && value != 0x87654321) {
            fprintf(stderr, "Unexpected byte-test; stop without further controller reads.\n");
            close(fd); return 1;
        }
    }
    close(fd);
    return 0;
}

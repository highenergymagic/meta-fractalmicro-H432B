/* SPDX-License-Identifier: MIT */
/* Transient MP01[5] chip-select test; restores that nibble before returning.
 * No FIFO, reset, clock, EEPROM, NAND or regulator writes. */
#define _FILE_OFFSET_BITS 64
#define _POSIX_C_SOURCE 200809L
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#define MUX_MASK (0xfU << 20)
static uint32_t read16(volatile const uint16_t *base, unsigned offset)
{
    uint32_t low = base[offset / 2];
    uint32_t high = base[offset / 2 + 1];
    return low | (high << 16);
}
int main(int argc, char **argv)
{
    if (argc != 2 || strcmp(argv[1], "--test-and-restore")) return 2;
    int fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0) { perror("/dev/mem"); return 1; }
    void *gpio = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0xe0200000);
    void *chip = mmap(NULL, 4096, PROT_READ, MAP_SHARED, fd, 0xa8000000);
    if (gpio == MAP_FAILED || chip == MAP_FAILED) {
        perror("mmap"); close(fd); return 1;
    }
    volatile uint32_t *mux = (volatile uint32_t *)((unsigned char *)gpio + 0x2e0);
    uint32_t saved = *mux;
    if ((saved & MUX_MASK) != (5U << 20)) {
        fprintf(stderr, "Refusing unexpected MP01[5] state: %08x\n", saved);
        munmap(gpio, 4096); munmap(chip, 4096); close(fd); return 1;
    }
    printf("MP01CON before=%08x; temporarily select only pin5 function2\n", saved);
    fflush(stdout);
    *mux = (saved & ~MUX_MASK) | (2U << 20);
    __sync_synchronize();
    uint32_t selected = *mux;
    struct timespec pause = {0, 1000000};
    nanosleep(&pause, NULL);
    uint32_t byte_test = read16(chip, 0x64);
    uint32_t id = 0, irq = 0, hw = 0, pmt = 0;
    if (byte_test == 0x87654321) {
        id = read16(chip, 0x50); irq = read16(chip, 0x54);
        hw = read16(chip, 0x74); pmt = read16(chip, 0x84);
    }
    /* Preserve unrelated bits even if another actor changed them meanwhile. */
    *mux = (*mux & ~MUX_MASK) | (saved & MUX_MASK);
    __sync_synchronize();
    uint32_t restored = *mux;
    printf("selected=%08x restored=%08x BYTE_TEST=%08x ID_REV=%08x IRQ_CFG=%08x HW_CFG=%08x PMT_CTRL=%08x\n",
           selected, restored, byte_test, id, irq, hw, pmt);
    munmap(gpio, 4096); munmap(chip, 4096); close(fd);
    return byte_test == 0x87654321 && (restored & MUX_MASK) == (saved & MUX_MASK) ? 0 : 1;
}

/* SPDX-License-Identifier: MIT */
/* Fixed two-register read, not a scanner or general PMIC programming tool.
 * The write phase selects a register address; it contains no register value.
 */
#define _POSIX_C_SOURCE 200809L
#include <fcntl.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    (void)argv;
    if (argc != 1) return 2;
    FILE *name = fopen("/sys/class/i2c-dev/i2c-9/name", "r");
    char buf[80] = {0};
    if (!name) { perror("adapter identity"); return 1; }
    int valid = fgets(buf, sizeof(buf), name) &&
                !strcmp(buf, "i2c-pmic-inventory\n");
    fclose(name);
    if (!valid) { fputs("Unexpected adapter; refusing transaction\n", stderr); return 1; }
    int fd = open("/dev/i2c-9", O_RDWR | O_CLOEXEC);
    if (fd < 0) { perror("i2c-9"); return 1; }
    unsigned long funcs = 0;
    if (ioctl(fd, I2C_FUNCS, &funcs) < 0 || !(funcs & I2C_FUNC_I2C)) {
        fputs("Combined transfers unavailable\n", stderr); close(fd); return 1;
    }
    for (uint8_t reg = 0; reg < 2; reg++) {
        uint8_t value = 0;
        struct i2c_msg msgs[2] = {
            {.addr = 0x66, .flags = 0, .len = 1, .buf = &reg},
            {.addr = 0x66, .flags = I2C_M_RD, .len = 1, .buf = &value},
        };
        struct i2c_rdwr_ioctl_data tx = {.msgs = msgs, .nmsgs = 2};
        if (ioctl(fd, I2C_RDWR, &tx) != 2) {
            perror("PMIC control read"); close(fd); return 1;
        }
        printf("PMIC 0x66 register 0x%02x = 0x%02x\n", reg, value);
    }
    return close(fd) ? 1 : 0;
}

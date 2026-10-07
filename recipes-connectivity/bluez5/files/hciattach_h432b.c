// SPDX-License-Identifier: GPL-2.0-or-later
/* Linux termios2 adapter kept separate from libc's struct termios. */
#include <asm/termbits.h>
#include <sys/ioctl.h>
#include <errno.h>
int h432b_set_custom_baud(int fd, unsigned int rate)
{
    struct termios2 tio;
    if (rate != 1382400)
        return -EINVAL;
    if (ioctl(fd, TCGETS2, &tio) < 0)
        return -errno;
    tio.c_cflag &= ~(CBAUD | CIBAUD);
    tio.c_cflag |= BOTHER | (BOTHER << IBSHIFT);
    tio.c_ispeed = rate;
    tio.c_ospeed = rate;
    if (ioctl(fd, TCSETS2, &tio) < 0)
        return -errno;
    if (ioctl(fd, TCGETS2, &tio) < 0)
        return -errno;
    if (tio.c_ispeed != rate || tio.c_ospeed != rate)
        return -ERANGE;
    return 0;
}

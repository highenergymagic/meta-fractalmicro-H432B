// SPDX-License-Identifier: MIT
/* Opt-in 100/300 ms diagnostic; not a production haptics interface. */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <linux/gpio.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t interrupted;
static void stop(int signal) { (void)signal; interrupted = 1; }
static int board_ok(void)
{
    char b[512];
    const char expected[] = "hims,braillesense-u2";
    ssize_t n;
    size_t i;
    int fd = open("/sys/firmware/devicetree/base/compatible", O_RDONLY);
    if (fd < 0) return 0;
    n = read(fd, b, sizeof(b));
    close(fd);
    for (i=0; n>0 && i+sizeof(expected)<=(size_t)n; i++)
        if ((!i || !b[i-1]) && !memcmp(b+i,expected,sizeof(expected))) return 1;
    return 0;
}
static int readback(int fd, unsigned expected)
{
    struct gpio_v2_line_values v = {.mask=1};
    if (ioctl(fd,GPIO_V2_LINE_GET_VALUES_IOCTL,&v)) return -1;
    printf("Motor GPIO readback=%u expected=%u\n",(unsigned)v.bits,expected);
    fflush(stdout);
    if (v.bits != expected) { errno=EIO; return -1; }
    return 0;
}
static int set(int fd, int on)
{
    struct gpio_v2_line_values v = {.mask=1, .bits=(unsigned)on};
    return ioctl(fd,GPIO_V2_LINE_SET_VALUES_IOCTL,&v);
}
int main(int argc, char **argv)
{
    struct gpio_v2_line_request req = {0};
    struct sigaction sa = {.sa_handler=stop};
    struct timespec duration = {.tv_sec=0, .tv_nsec=100000000};
    glob_t paths;
    size_t i;
    int chip=-1, line=-1, rc=1;
    if (argc!=2 || (strcmp(argv[1],"--pulse-100ms") && strcmp(argv[1],"--pulse-300ms"))) {
        fprintf(stderr,"Usage: %s --pulse-100ms | --pulse-300ms\n",argv[0]);
        return 2;
    }
    if (!strcmp(argv[1],"--pulse-300ms")) duration.tv_nsec=300000000;
    if (!board_ok()) { fputs("Wrong or unidentified board\n",stderr); return 1; }
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGINT,&sa,NULL) || sigaction(SIGTERM,&sa,NULL)) return 1;
    if (glob("/dev/gpiochip*",0,NULL,&paths)) return 1;
    for (i=0;i<paths.gl_pathc;i++) {
        struct gpiochip_info info = {0};
        int fd = open(paths.gl_pathv[i],O_RDONLY|O_CLOEXEC);
        if (fd<0) continue;
        if (!ioctl(fd,GPIO_GET_CHIPINFO_IOCTL,&info) &&
            !strcmp(info.label,"gpe1") && info.lines>4) {
            if (chip>=0) { close(fd); goto out; }
            chip=fd;
        } else close(fd);
    }
    if (chip<0) { fputs("Missing GPE1 bank\n",stderr); goto out; }
    req.offsets[0]=4;
    req.num_lines=1;
    strcpy(req.consumer,"h432b-vibrator-test");
    req.config.flags=GPIO_V2_LINE_FLAG_OUTPUT | GPIO_V2_LINE_FLAG_BIAS_DISABLED;
    req.config.num_attrs=1;
    req.config.attrs[0].attr.id=GPIO_V2_LINE_ATTR_ID_OUTPUT_VALUES;
    req.config.attrs[0].attr.values=0;
    req.config.attrs[0].mask=1;
    if (ioctl(chip,GPIO_V2_GET_LINE_IOCTL,&req)) { perror("claim motor line"); goto out; }
    line=req.fd;
    if (interrupted) goto out;
    if (readback(line,0)) { perror("initial readback"); goto out; }
    printf("One %ld ms motor pulse now\n",duration.tv_nsec/1000000); fflush(stdout);
    if (set(line,1)) { perror("motor on"); goto out; }
    if (readback(line,1)) { perror("active readback"); goto out; }
    while (!interrupted && nanosleep(&duration,&duration))
        if (errno!=EINTR) { perror("pulse delay"); goto out; }
    rc=0;
out:
    if (line>=0) {
        if (set(line,0) || readback(line,0)) { perror("motor off"); rc=1; }
        close(line);
    }
    if (chip>=0) close(chip);
    globfree(&paths);
    if (!rc) puts("Motor output returned low");
    return rc;
}

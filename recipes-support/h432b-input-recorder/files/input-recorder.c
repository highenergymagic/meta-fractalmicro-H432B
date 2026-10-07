// SPDX-License-Identifier: MIT
/* Bounded bring-up recorder. Uses exclusive GPIO line ownership, not /dev/mem.
 * No key injection, storage writes, PMIC access, display or motor control. */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <linux/gpio.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

struct bank {
    const char *label;
    unsigned first, count;
    int chip, lines;
};
static struct bank banks[] = {
    {"gpj0",0,3,-1,-1}, {"gpj1",0,4,-1,-1},
    {"gpj2",0,8,-1,-1}, {"gpj3",0,8,-1,-1},
    {"gph0",5,2,-1,-1}, {"gph2",1,5,-1,-1}
};
struct sample { unsigned row[3], direct, selectors; };
static volatile sig_atomic_t stopped;
static void stop_recording(int sig) { (void)sig; stopped = 1; }
static unsigned row_drive(unsigned row) { return 7U & ~(1U << row); }
static unsigned pressed(unsigned j2, unsigned j3)
{ return ((~j2)&255U) | (((~j3)&255U)<<8); }
static double now(void)
{
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC,&t)) { perror("clock_gettime"); exit(1); }
    return t.tv_sec + t.tv_nsec/1e9;
}
static void pause_us(long us)
{
    struct timespec t = {0,us*1000};
    while (!stopped && nanosleep(&t,&t) && errno == EINTR) {}
}
static int board_check(void)
{
    char data[512];
    const char wanted[] = "hims,braillesense-u2";
    ssize_t n;
    size_t i;
    int fd = open("/sys/firmware/devicetree/base/compatible",O_RDONLY|O_CLOEXEC);
    if (fd < 0) return -1;
    n = read(fd,data,sizeof(data));
    close(fd);
    for (i=0;n>0 && i+sizeof(wanted)<=(size_t)n;i++)
        if ((!i || !data[i-1]) && !memcmp(data+i,wanted,sizeof(wanted))) return 0;
    fprintf(stderr,"Refusing to scan: unsupported board compatible\n");
    return -1;
}
static int discover(void)
{
    glob_t paths;
    unsigned b;
    size_t p;
    int rc = -1;
    if (glob("/dev/gpiochip*",0,NULL,&paths)) return -1;
    for (p=0;p<paths.gl_pathc;p++) {
        struct gpiochip_info info;
        int fd = open(paths.gl_pathv[p],O_RDONLY|O_CLOEXEC);
        if (fd < 0) continue;
        memset(&info,0,sizeof(info));
        if (ioctl(fd,GPIO_GET_CHIPINFO_IOCTL,&info)) { close(fd); continue; }
        for (b=0;b<sizeof(banks)/sizeof(banks[0]);b++) {
            if (!strcmp(info.label,banks[b].label)) {
                if (banks[b].chip >= 0 || info.lines < banks[b].first+banks[b].count) {
                    fprintf(stderr,"Ambiguous or undersized GPIO bank %s\n",info.label);
                    close(fd); goto out;
                }
                banks[b].chip = fd;
                fd = -1;
                break;
            }
        }
        if (fd >= 0) close(fd);
    }
    for (b=0;b<sizeof(banks)/sizeof(banks[0]);b++)
        if (banks[b].chip < 0) {
            fprintf(stderr,"Missing GPIO bank %s\n",banks[b].label);
            goto out;
        }
    rc = 0;
out:
    globfree(&paths);
    return rc;
}
static int request_bank(unsigned b)
{
    struct gpio_v2_line_request req;
    unsigned i;
    memset(&req,0,sizeof(req));
    req.num_lines = banks[b].count;
    strcpy(req.consumer,"h432b-input-recorder");
    for (i=0;i<req.num_lines;i++) req.offsets[i] = banks[b].first+i;
    req.config.flags = GPIO_V2_LINE_FLAG_BIAS_DISABLED |
                      (b ? GPIO_V2_LINE_FLAG_INPUT : GPIO_V2_LINE_FLAG_OUTPUT);
    if (!b) {
        req.config.num_attrs = 1;
        req.config.attrs[0].attr.id = GPIO_V2_LINE_ATTR_ID_OUTPUT_VALUES;
        req.config.attrs[0].attr.values = 7; /* All scan rows idle, atomically. */
        req.config.attrs[0].mask = 7;
    }
    if (ioctl(banks[b].chip,GPIO_V2_GET_LINE_IOCTL,&req)) {
        fprintf(stderr,"Cannot own %s[%u..%u]: %s\n",banks[b].label,
                banks[b].first,banks[b].first+banks[b].count-1,strerror(errno));
        return -1;
    }
    banks[b].lines = req.fd;
    return 0;
}
static int read_bank(unsigned b, unsigned *value)
{
    struct gpio_v2_line_values v = {.mask=(1ULL<<banks[b].count)-1};
    if (ioctl(banks[b].lines,GPIO_V2_LINE_GET_VALUES_IOCTL,&v)) return -1;
    *value = (unsigned)v.bits;
    return 0;
}
static int drive(unsigned value)
{
    struct gpio_v2_line_values v = {.bits=value,.mask=7};
    return ioctl(banks[0].lines,GPIO_V2_LINE_SET_VALUES_IOCTL,&v);
}
static int scan(struct sample *s)
{
    int row;
    unsigned a,b,h0,h2;
    if (read_bank(1,&s->selectors) || read_bank(4,&h0) || read_bank(5,&h2))
        return -1;
    s->direct = ((~h2)&31U) | (((~h0)&3U)<<5);
    /* Stock order: GPJ0[2], then [1], then [0]; release between rows. */
    for (row=2;row>=0;row--) {
        if (drive(row_drive((unsigned)row))) return -1;
        pause_us(50);
        if (read_bank(2,&a) || read_bank(3,&b)) {
            (void)drive(7); return -1;
        }
        s->row[row] = pressed(a,b);
        if (drive(7)) return -1;
    }
    return 0;
}
static int equal(const struct sample *a,const struct sample *b)
{
    return a->row[0]==b->row[0] && a->row[1]==b->row[1] &&
           a->row[2]==b->row[2] && a->direct==b->direct &&
           a->selectors==b->selectors;
}
static void print_sample(double elapsed,const struct sample *s)
{
    printf("t=%.3f row0=%04x row1=%04x row2=%04x direct=%02x selector01=%u selector23=%u\n",
           elapsed,s->row[0],s->row[1],s->row[2],s->direct,
           s->selectors&3U,(s->selectors>>2)&3U);
    fflush(stdout);
}
static int cleanup(void)
{
    unsigned b;
    int rc = 0;
    if (banks[0].lines >= 0 && drive(7)) { perror("restore scan rows idle"); rc = -1; }
    for (b=0;b<sizeof(banks)/sizeof(banks[0]);b++) {
        if (banks[b].lines >= 0) close(banks[b].lines);
        if (banks[b].chip >= 0) close(banks[b].chip);
    }
    return rc;
}
static int self_test(void)
{
    unsigned row,bit;
    struct sample a = {0},b = {0};
    assert(equal(&a,&b));
    b.direct=1; assert(!equal(&a,&b)); b.direct=0;
    b.selectors=1; assert(!equal(&a,&b)); b.selectors=0;
    for (row=0;row<3;row++) {
        assert((row_drive(row)^7U)==(1U<<row));
        b.row[row]=1; assert(!equal(&a,&b)); b.row[row]=0;
    }
    assert(pressed(255,255)==0);
    for (bit=0;bit<8;bit++) {
        assert(pressed(255U^(1U<<bit),255)==(1U<<bit));
        assert(pressed(255,255U^(1U<<bit))==(1U<<(bit+8)));
    }
    assert(pressed(0,0)==65535);
    puts("input recorder pure-logic tests passed");
    return 0;
}
int main(int argc,char **argv)
{
    struct sample candidate = {0}, stable = {0}, s;
    struct sigaction sa = {.sa_handler=stop_recording};
    char *end;
    long seconds;
    double start;
    unsigned b;
    int have_candidate=0,have_stable=0,rc=1;
    if (argc==2 && !strcmp(argv[1],"--self-test")) return self_test();
    if (argc!=3 || strcmp(argv[1],"--record")) {
        fprintf(stderr,"Usage: %s --self-test | --record SECONDS (1..180)\n"
                "Recording actively scans only GPJ0[0..2], returning rows high.\n",
                argv[0]);
        return 2;
    }
    errno=0; seconds=strtol(argv[2],&end,10);
    if (errno || !*argv[2] || *end || seconds<1 || seconds>180) return 2;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGINT,&sa,NULL) || sigaction(SIGTERM,&sa,NULL)) return 1;
    if (board_check() || discover()) goto out;
    /* Own all inputs before enabling scan outputs; reject busy lines. */
    for (b=1;b<sizeof(banks)/sizeof(banks[0]);b++)
        if (request_bank(b)) goto out;
    if (request_bank(0)) goto out;
    puts("READY: active-low key masks; raw selector pairs; no key events injected");
    fflush(stdout);
    start=now();
    while (!stopped && now()-start<seconds) {
        if (scan(&s)) { perror("GPIO scan"); goto out; }
        if (have_candidate && equal(&s,&candidate)) {
            if (!have_stable || !equal(&s,&stable)) {
                stable=s; have_stable=1; print_sample(now()-start,&stable);
            }
        } else {
            candidate=s; have_candidate=1;
        }
        pause_us(10000);
    }
    rc=0;
out:
    if (cleanup()) rc=1;
    if (!rc) puts("DONE: scan rows returned idle; no persistent storage changed");
    return rc;
}

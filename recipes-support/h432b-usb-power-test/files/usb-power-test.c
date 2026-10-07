// SPDX-License-Identifier: MIT
/* Explicit, temporary board USB-enable diagnostic; no PMIC or storage writes. */
#define _POSIX_C_SOURCE 200809L
#define _FILE_OFFSET_BITS 64
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <linux/gpio.h>
#include <signal.h>
#include <stdint.h>
#include <sys/mman.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

struct control { const char *bank; unsigned pin; int fd; unsigned saved; };
static struct control controls[] = {
    {"gph2", 7, -1, 0}, /* Stock HUB enable */
    {"gph3", 7, -1, 0}, /* Stock USB12 enable */
    {"gph1", 3, -1, 0}, /* Stock USB3 enable */
    {"gpj4", 2, -1, 0}, /* Stock HUB reset: low 100ms, then high */
};
static unsigned saved_bias[4];
static unsigned ncontrols=3;
static volatile sig_atomic_t interrupted;
static void stop(int sig) { (void)sig; interrupted=1; }
static int board_ok(void)
{
    const char expected[]="hims,braillesense-u2";
    char buf[512]; ssize_t n; size_t i;
    int fd=open("/sys/firmware/devicetree/base/compatible",O_RDONLY);
    if (fd<0) return 0;
    n=read(fd,buf,sizeof(buf)); close(fd);
    for(i=0;n>0 && i+sizeof(expected)<=(size_t)n;i++)
        if ((!i || !buf[i-1]) && !memcmp(buf+i,expected,sizeof(expected))) return 1;
    return 0;
}
static int snapshot(int capture)
{
    const unsigned offsets[]={0xc40,0xc60,0xc20,0x2c0};
    int fd=open("/dev/mem",O_RDONLY|O_SYNC|O_CLOEXEC);
    if(fd<0) return -1;
    volatile uint32_t *regs=mmap(NULL,4096,PROT_READ,MAP_SHARED,fd,0xe0200000ULL);
    close(fd);
    if(regs==MAP_FAILED) return -1;
    int valid=1;
    for(unsigned i=0;i<ncontrols;i++) {
        unsigned o=offsets[i],p=controls[i].pin;
        printf("%s[%u]: mux=%u level=%u pull=%u CON=%08x DAT=%08x\n",
               controls[i].bank,p,(regs[o/4]>>(4*p))&15,
               (regs[(o+4)/4]>>p)&1,(regs[(o+8)/4]>>(2*p))&3,
               regs[o/4],regs[(o+4)/4]);
        if(((regs[o/4]>>(4*p))&15)!=1) valid=0;
        unsigned bias=(regs[(o+8)/4]>>(2*p))&3;
        if(bias==2) valid=0;
        if(capture) saved_bias[i]=bias;
    }
    printf("SYSTEM5V GPE1[1]: mux=%u level=%u (read only)\n",
           (regs[0x100/4]>>4)&15,(regs[0x104/4]>>1)&1);
    munmap((void *)regs,4096);
    if(!valid) { errno=EINVAL; return -1; }
    return 0;
}
static int host_snapshot(void)
{
    const uint64_t bases[]={0xec100000ULL,0xec200000ULL,0xec300000ULL};
    const unsigned counts[]={3,7,8};
    const unsigned offsets[][8]={{0,4,8},
        {0,4,0x10,0x14,0x18,0x50,0x54},
        {0,4,8,0xc,0x48,0x4c,0x50,0x54}};
    int fd=open("/dev/mem",O_RDONLY|O_SYNC|O_CLOEXEC);
    if(fd<0) return -1;
    for(unsigned bank=0;bank<3;bank++) {
        volatile uint32_t *r=mmap(NULL,4096,PROT_READ,MAP_SHARED,fd,bases[bank]);
        if(r==MAP_FAILED) { close(fd); return -1; }
        for(unsigned j=0;j<counts[bank];j++)
            printf("MMIO %08llx=%08x\n",
                   (unsigned long long)(bases[bank]+offsets[bank][j]),r[offsets[bank][j]/4]);
        munmap((void *)r,4096);
    }
    close(fd); fflush(stdout); return 0;
}
static int value(int fd, unsigned *out);
static int configure_bias(struct control *c,unsigned bias)
{
    struct gpio_v2_line_config cfg={.flags=GPIO_V2_LINE_FLAG_OUTPUT,.num_attrs=1};
    unsigned current;
    if(value(c->fd,&current)) return -1;
    cfg.flags |= bias==0 ? GPIO_V2_LINE_FLAG_BIAS_DISABLED :
                 bias==1 ? GPIO_V2_LINE_FLAG_BIAS_PULL_DOWN : GPIO_V2_LINE_FLAG_BIAS_PULL_UP;
    cfg.attrs[0].attr.id=GPIO_V2_LINE_ATTR_ID_OUTPUT_VALUES;
    cfg.attrs[0].attr.values=current; cfg.attrs[0].mask=1;
    return ioctl(c->fd,GPIO_V2_LINE_SET_CONFIG_IOCTL,&cfg);
}
static int value(int fd, unsigned *out)
{
    struct gpio_v2_line_values v={.mask=1};
    if(ioctl(fd,GPIO_V2_LINE_GET_VALUES_IOCTL,&v)) return -1;
    *out=(unsigned)(v.bits & 1);
    return 0;
}
static int write_value(struct control *c,unsigned v)
{
    struct gpio_v2_line_values vals={.mask=1,.bits=v};
    unsigned actual;
    if(ioctl(c->fd,GPIO_V2_LINE_SET_VALUES_IOCTL,&vals) ||
       value(c->fd,&actual)) return -1;
    printf("%s[%u]=%u (requested %u)\n",c->bank,c->pin,actual,v);
    fflush(stdout);
    if(actual!=v) { errno=EIO; return -1; }
    return 0;
}
static int claim(struct control *c)
{
    glob_t paths;
    int chip=-1, rc=-1;
    if(glob("/dev/gpiochip*",0,NULL,&paths)) return -1;
    for(size_t i=0;i<paths.gl_pathc;i++) {
        struct gpiochip_info ci={0};
        int fd=open(paths.gl_pathv[i],O_RDONLY|O_CLOEXEC);
        if(fd<0) continue;
        if(!ioctl(fd,GPIO_GET_CHIPINFO_IOCTL,&ci) && !strcmp(ci.label,c->bank)) {
            if(chip>=0) { close(fd); goto out; }
            chip=fd;
        } else close(fd);
    }
    if(chip<0) goto out;
    struct gpio_v2_line_info info={.offset=c->pin};
    if(ioctl(chip,GPIO_V2_GET_LINEINFO_IOCTL,&info)) goto out;
    /* Raw snapshot checks output mux: this driver lacks get_direction. */
    if(info.flags & (GPIO_V2_LINE_FLAG_USED | GPIO_V2_LINE_FLAG_ACTIVE_LOW)) {
        fprintf(stderr,"%s[%u] is not a free ordinary output\n",c->bank,c->pin);
        errno=EBUSY; goto out;
    }
    struct gpio_v2_line_request req={0};
    req.offsets[0]=c->pin; req.num_lines=1;
    strcpy(req.consumer,"h432b-usb-power-test");
    /* Flags zero preserves direction and output state for initial read. */
    if(ioctl(chip,GPIO_V2_GET_LINE_IOCTL,&req)) goto out;
    c->fd=req.fd;
    if(value(c->fd,&c->saved)) { close(c->fd); c->fd=-1; goto out; }
    printf("INITIAL %s[%u]=%u\n",c->bank,c->pin,c->saved); fflush(stdout);
    /* Mark output explicitly, retaining the value and existing bias. */
    struct gpio_v2_line_config cfg={.flags=GPIO_V2_LINE_FLAG_OUTPUT,.num_attrs=1};
    cfg.attrs[0].attr.id=GPIO_V2_LINE_ATTR_ID_OUTPUT_VALUES;
    cfg.attrs[0].attr.values=c->saved;
    cfg.attrs[0].mask=1;
    if(ioctl(c->fd,GPIO_V2_LINE_SET_CONFIG_IOCTL,&cfg)) goto out;
    rc=0;
out:
    if(chip>=0) close(chip);
    globfree(&paths);
    return rc;
}
int main(int argc,char **argv)
{
    int enable,stock,reset,rc=1;
    if(argc!=2 || (strcmp(argv[1],"--read") && strcmp(argv[1],"--enable-20s") && strcmp(argv[1],"--stock-bias-20s") && strcmp(argv[1],"--hub-reset-20s"))) {
        fprintf(stderr,"Usage: %s --read | --enable-20s | --stock-bias-20s | --hub-reset-20s\n",argv[0]); return 2;
    }
    reset=!strcmp(argv[1],"--hub-reset-20s");
    stock=reset || !strcmp(argv[1],"--stock-bias-20s");
    ncontrols=(reset || !strcmp(argv[1],"--read")) ? 4 : 3;
    enable=stock || !strcmp(argv[1],"--enable-20s");
    if(!board_ok()) { fputs("Wrong or unidentified board\n",stderr); return 1; }
    if(snapshot(1)) { perror("GPIO snapshot/output guard"); return 1; }
    if(!enable) return host_snapshot() ? 1 : 0;
    struct sigaction sa={.sa_handler=stop};
    sigemptyset(&sa.sa_mask);
    if(sigaction(SIGINT,&sa,NULL) || sigaction(SIGTERM,&sa,NULL) ||
       sigaction(SIGHUP,&sa,NULL)) return 1;
    for(unsigned i=0;i<ncontrols;i++)
        if(claim(&controls[i])) { perror("claim USB output"); goto out; }
    if(enable) {
        if(stock) for(unsigned i=0;i<ncontrols;i++)
            if(configure_bias(&controls[i],0)) { perror("disable bias"); goto out; }
        if(reset && write_value(&controls[3],0)) goto out;
        for(unsigned i=0;i<3;i++) {
            if(interrupted || write_value(&controls[i],1)) goto out;
        }
        if(reset) {
            struct timespec pulse={.tv_nsec=100000000};
            while(!interrupted && nanosleep(&pulse,&pulse))
                if(errno!=EINTR) goto out;
            if(interrupted || write_value(&controls[3],1)) goto out;
        }
        if(snapshot(0) || host_snapshot()) { perror("active snapshot"); goto out; }
        puts("USB enables held for at most 20 seconds"); fflush(stdout);
        struct timespec delay={.tv_sec=20};
        while(!interrupted && nanosleep(&delay,&delay))
            if(errno!=EINTR) { perror("delay"); goto out; }
    }
    if(host_snapshot()) goto out;
    rc=interrupted ? 1:0;
out:
    /* Reverse order: port enables before hub. Preserve original values. */
    for(int i=(int)ncontrols-1;i>=0;i--) if(controls[i].fd>=0) {
        if(write_value(&controls[i],controls[i].saved)) { perror("restore"); rc=1; }
        if(stock && configure_bias(&controls[i],saved_bias[i])) { perror("restore bias"); rc=1; }
        close(controls[i].fd);
    }
    if(snapshot(0)) rc=1;
    if(!rc) puts("USB original output states restored");
    return rc;
}

// SPDX-License-Identifier: MIT
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <linux/input.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/ioctl.h>

static const char *names[] = {
    "H432B keyboard and controls", "H432B braille routing", "H432B selectors"
};
static double now(void)
{
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC,&t)) return -1;
    return t.tv_sec+t.tv_nsec/1e9;
}
int main(void)
{
    struct pollfd fds[3] = {{.fd=-1},{.fd=-1},{.fd=-1}};
    glob_t paths;
    size_t p;
    int i,rc=1;
    double start,t;
    if (glob("/dev/input/event*",0,NULL,&paths)) return 1;
    setvbuf(stdout,NULL,_IOLBF,0);
    for (p=0;p<paths.gl_pathc;p++) {
        char name[128]={0};
        int fd=open(paths.gl_pathv[p],O_RDONLY|O_NONBLOCK|O_CLOEXEC);
        if (fd<0) continue;
        if (ioctl(fd,EVIOCGNAME(sizeof(name)),name)<0) { close(fd); continue; }
        for (i=0;i<3;i++) {
            if (strcmp(name,names[i])) continue;
            if (fds[i].fd>=0) { close(fd); goto out; }
            fds[i].fd=fd; fds[i].events=POLLIN; fd=-1; break;
        }
        if (fd>=0) close(fd);
    }
    for (i=0;i<3;i++)
        if (fds[i].fd<0) { fprintf(stderr,"Missing %s\n",names[i]); goto out; }
    for (i=0;i<2;i++) {
        unsigned char keys[(KEY_MAX+8)/8]={0};
        unsigned int bit;
        if (ioctl(fds[i].fd,EVIOCGKEY(sizeof(keys)),keys)<0) goto out;
        for (bit=0;bit<=KEY_MAX;bit++)
            if (keys[bit/8] & (1U<<(bit%8))) printf("INITIAL device=%d held=%u\n",i,bit);
    }
    for (i=0;i<2;i++) {
        struct input_absinfo axis;
        unsigned code=i ? ABS_RZ : ABS_MISC;
        if (ioctl(fds[2].fd,EVIOCGABS(code),&axis)<0) goto out;
        printf("INITIAL selector code=%u value=%d\n",code,axis.value);
    }
    start=now();
    if (start<0) goto out;
    puts("READY: 180 seconds; read-only evdev, no grab or injected events");
    while ((t=now())>=0 && t-start<180) {
        int n=poll(fds,3,250);
        if (n<0) { if (errno==EINTR) continue; goto out; }
        for (i=0;i<3;i++) {
            struct input_event ev;
            ssize_t size;
            if (fds[i].revents & (POLLHUP|POLLERR|POLLNVAL)) goto out;
            if (!(fds[i].revents & POLLIN)) continue;
            while ((size=read(fds[i].fd,&ev,sizeof(ev)))==(ssize_t)sizeof(ev)) {
                if (ev.type==EV_SYN && ev.code==SYN_DROPPED) {
                    fputs("Event loss; test invalid, resynchronization required\n",stderr);
                    goto out;
                }
                printf("EVENT t=%.3f device=%d type=%u code=%u value=%d\n",
                       now()-start,i,ev.type,ev.code,ev.value);
            }
            if (size>=0 || errno!=EAGAIN) goto out;
        }
    }
    if (t<0) goto out;
    puts("DONE");
    rc=0;
out:
    for (i=0;i<3;i++) if (fds[i].fd>=0) close(fds[i].fd);
    globfree(&paths);
    return rc;
}

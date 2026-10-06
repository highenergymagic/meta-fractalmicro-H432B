// SPDX-License-Identifier: MIT
#define _POSIX_C_SOURCE 200809L
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/ioctl.h>
static double now(void) {
 struct timespec t;
 if (clock_gettime(CLOCK_MONOTONIC, &t)) return -1;
 return t.tv_sec + t.tv_nsec / 1e9;
}
int main(void) {
 char name[128] = {0};
 struct input_event ev;
 int fd = open("/dev/input/event0", O_RDONLY | O_NONBLOCK);
 if (fd < 0) { perror("open"); return 1; }
 if (ioctl(fd, EVIOCGNAME(sizeof(name)), name) < 0 ||
     strcmp(name, "H432B power switch")) {
  fprintf(stderr, "Unexpected input device: %s\n", name);
  close(fd); return 1;
 }
 setvbuf(stdout, NULL, _IOLBF, 0);
 printf("ARMED %s; 120 seconds; no input grab or power action\n", name);
 double start = now();
 if (start < 0) return 1;
 while (now() - start < 120) {
  struct pollfd p = {fd, POLLIN, 0};
  int rc = poll(&p, 1, 250);
  if (rc < 0 || (rc > 0 && (p.revents & (POLLERR|POLLHUP|POLLNVAL)))) {
   perror("poll/device"); close(fd); return 1;
  }
  if (rc > 0 && (p.revents & POLLIN)) {
   ssize_t n = read(fd, &ev, sizeof(ev));
   if (n != sizeof(ev)) { perror("read event"); close(fd); return 1; }
   printf("EVENT t=%.3f type=%u code=%u value=%d\n",
          now()-start, ev.type, ev.code, ev.value);
  }
 }
 close(fd);
 puts("DONE");
 return 0;
}

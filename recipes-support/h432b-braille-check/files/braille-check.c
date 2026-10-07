/* SPDX-License-Identifier: MIT */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
int main(int argc, char **argv)
{
	unsigned char cells[32] = {0};
	const unsigned char greeting[] = {32,7,10,29,37,45,0,3,23,1,10,7,7,17};
	int fd;
	ssize_t count;
	if (argc != 2) {
		fprintf(stderr, "Usage: %s --greeting|--bottom-dots|--blank|--abi-check\n", argv[0]);
		return 2;
	}
	if (!strcmp(argv[1], "--greeting"))
		memcpy(cells, greeting, sizeof(greeting));
	else if (!strcmp(argv[1], "--bottom-dots")) {
		cells[0] = 0x40;
		cells[2] = 0x80;
	} else if (strcmp(argv[1], "--blank") && strcmp(argv[1], "--abi-check"))
		return 2;
	fd = open("/dev/h432b-braille", O_WRONLY | O_CLOEXEC);
	if (fd < 0) { perror("display open"); return 1; }
	if (!strcmp(argv[1], "--abi-check")) {
		int other = open("/dev/h432b-braille", O_WRONLY | O_CLOEXEC);
		if (other >= 0 || errno != EBUSY) {
			if (other >= 0) close(other);
			close(fd);
			fprintf(stderr, "exclusive-open contract failed\n");
			return 1;
		}
		errno = 0;
		count = write(fd, cells, 31);
		if (count != -1 || errno != EINVAL) {
			close(fd);
			fprintf(stderr, "frame-length contract failed\n");
			return 1;
		}
		puts("BRAILLE_ABI_PASS");
		close(fd);
		return 0;
	}
	do { count = write(fd, cells, sizeof(cells)); } while (count < 0 && errno == EINTR);
	close(fd);
	if (count != sizeof(cells)) { perror("display write"); return 1; }
	puts("BRAILLE_FRAME_ACCEPTED");
	return 0;
}

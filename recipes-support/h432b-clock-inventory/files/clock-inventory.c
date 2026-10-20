/* SPDX-License-Identifier: MIT */
/* Fixed, read-only S5PV210 clock/DMC and H432B selector inventory. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

struct register_word {
	const char *name;
	unsigned int offset;
};

static int dump_page(int fd, off_t base, const struct register_word *words,
		     size_t count)
{
	void *page = mmap(NULL, 4096, PROT_READ, MAP_SHARED, fd, base);
	size_t i;

	if (page == MAP_FAILED) {
		perror("mmap");
		return -1;
	}
	for (i = 0; i < count; ++i) {
		const volatile uint32_t *reg =
			(const volatile uint32_t *)((const char *)page + words[i].offset);

		printf("%s=0x%08x\n", words[i].name, *reg);
		fflush(stdout);
	}
	return munmap(page, 4096);
}

int main(int argc, char **argv)
{
	static const struct register_word gpio[] = {
		{"GPH0CON", 0xc00}, {"GPH0DAT", 0xc04},
		{"GPH1CON", 0xc20}, {"GPH1DAT", 0xc24},
	};
	static const struct register_word clock[] = {
		{"APLL_CON", 0x100}, {"MPLL_CON", 0x108},
		{"CLK_SRC0", 0x200}, {"CLK_DIV0", 0x300}, {"CLK_DIV6", 0x318},
	};
	static const struct register_word dmc0[] = {
		{"DMC0_MEMCONTROL", 4}, {"DMC0_REFRESH", 0x30},
	};
	static const struct register_word mcs[] = {
		{"ARM_MCS_CON", 0x100},
	};
	static const struct register_word dmc1[] = {
		{"DMC1_MEMCONTROL", 4}, {"DMC1_REFRESH", 0x30},
	};
	char compatible[256];
	int fd, ret;
	ssize_t length;

	if (argc != 2 || strcmp(argv[1], "--read")) {
		fprintf(stderr, "Usage: %s --read\n", argv[0]);
		return 2;
	}
	fd = open("/sys/firmware/devicetree/base/compatible", O_RDONLY | O_CLOEXEC);
	if (fd < 0) {
		perror("board identity");
		return 1;
	}
	length = read(fd, compatible, sizeof(compatible));
	close(fd);
	if (length <= 0 || !memmem(compatible, length, "hims,braillesense-u2",
				   sizeof("hims,braillesense-u2"))) {
		fputs("Unsupported board\n", stderr);
		return 1;
	}
	fd = open("/dev/mem", O_RDONLY | O_SYNC | O_CLOEXEC);
	if (fd < 0) {
		perror("physical memory");
		return 1;
	}
	ret = dump_page(fd, 0xe0200000, gpio, sizeof(gpio) / sizeof(gpio[0]));
	if (!ret)
		ret = dump_page(fd, 0xe0100000, clock, sizeof(clock) / sizeof(clock[0]));
	if (!ret)
		ret = dump_page(fd, 0xe0106000, mcs, sizeof(mcs) / sizeof(mcs[0]));
	if (!ret)
		ret = dump_page(fd, 0xf0000000, dmc0, sizeof(dmc0) / sizeof(dmc0[0]));
	if (!ret)
		ret = dump_page(fd, 0xf1400000, dmc1, sizeof(dmc1) / sizeof(dmc1[0]));
	close(fd);
	return ret ? 1 : 0;
}

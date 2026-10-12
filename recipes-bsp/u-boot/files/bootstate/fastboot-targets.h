/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef H432B_FASTBOOT_TARGETS_H
#define H432B_FASTBOOT_TARGETS_H

#include <string.h>

enum h432b_fastboot_storage {
	H432B_FASTBOOT_UBI,
	H432B_FASTBOOT_MMC,
};

struct h432b_fastboot_target {
	enum h432b_fastboot_storage storage;
	unsigned int slot;
	const char *name;
};

/* Resolve explicit targets only; never silently select a slot for a writer.
 * Uppercase suffixes are compatibility aliases, not separate partitions.
 * MMC names are partition labels, not assumed partition numbers.
 */
static inline int
h432b_fastboot_resolve(const char *name, struct h432b_fastboot_target *out)
{
	static const struct {
		const char *alias;
		enum h432b_fastboot_storage storage;
		const char *volume[2];
	} targets[] = {
		{ "boot", H432B_FASTBOOT_UBI, { "kernel_a", "kernel_b" } },
		{ "kernel", H432B_FASTBOOT_UBI, { "kernel_a", "kernel_b" } },
		{ "system", H432B_FASTBOOT_UBI, { "systembase_a", "systembase_b" } },
		{ "systembase", H432B_FASTBOOT_UBI, { "systembase_a", "systembase_b" } },
		{ "systemext", H432B_FASTBOOT_MMC, { "systemext_a", "systemext_b" } },
	};
	unsigned int i, slot;
	size_t len;

	if (!name || !out)
		return -1;
	len = strlen(name);
	if (len < 3 || name[len - 2] != '_')
		return -1;
	switch (name[len - 1]) {
	case 'a':
	case 'A':
		slot = 0;
		break;
	case 'b':
	case 'B':
		slot = 1;
		break;
	default:
		return -1;
	}
	for (i = 0; i < sizeof(targets) / sizeof(targets[0]); i++) {
		if (strlen(targets[i].alias) != len - 2 ||
		    memcmp(name, targets[i].alias, len - 2))
			continue;
		out->storage = targets[i].storage;
		out->slot = slot;
		out->name = targets[i].volume[slot];
		return 0;
	}
	return -1;
}

#endif

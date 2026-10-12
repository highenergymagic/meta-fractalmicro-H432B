/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef H432B_SLOT_PAIR_H
#define H432B_SLOT_PAIR_H

#include <stddef.h>
#include <string.h>

struct h432b_verity {
	unsigned int blocks, bytes;
	unsigned char root[32], salt[32];
};

static inline int h432b_hex(unsigned char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

static inline int h432b_pair_decimal(const unsigned char **p, const unsigned char *end,
				     unsigned int *out, unsigned char delimiter)
{
	unsigned int n = 0, digits = 0;

	while (*p < end && **p >= '0' && **p <= '9') {
		if (++digits > 9)
			return -1;
		n = n * 10 + *(*p)++ - '0';
	}
	if (!digits || *p == end || *(*p)++ != delimiter)
		return -1;
	*out = n;
	return 0;
}

static inline int h432b_verity_parse(const unsigned char *p, size_t size,
				     struct h432b_verity *out)
{
	const unsigned char *end = p + size;
	unsigned int version, hash_start, i, j;
	unsigned char *digest;
	int a, b;

	if (h432b_pair_decimal(&p, end, &version, ' ') || version != 1 ||
	    h432b_pair_decimal(&p, end, &out->blocks, ' ') ||
	    out->blocks < 2 || out->blocks > 51200 ||
	    h432b_pair_decimal(&p, end, &hash_start, ' ') || hash_start != out->blocks)
		return -1;
	for (j = 0; j < 2; j++) {
		if (end - p < 65)
			return -1;
		digest = j ? out->salt : out->root;
		for (i = 0; i < 32; i++) {
			a = h432b_hex(*p++);
			b = h432b_hex(*p++);
			if (a < 0 || b < 0)
				return -1;
			digest[i] = (a << 4) | b;
		}
		if (*p++ != ' ')
			return -1;
	}
	if (h432b_pair_decimal(&p, end, &out->bytes, '\n') || p != end ||
	    out->bytes > 200U * 1024U * 1024U)
		return -1;
	return 0;
}

/* dm-verity v1: SHA256(salt || block), 128 digests/block, root level first.
 * The caller supplies the entire corrected image and a SHA-256 primitive.
 */
static inline int h432b_verity_verify(const unsigned char *image, size_t size,
				      const struct h432b_verity *v,
		void (*hash)(const unsigned char *, const unsigned char *, unsigned char *))
{
	unsigned int levels[8], offsets[8], count = v->blocks, n = 0;
	unsigned int total = v->blocks, i, j, input = 0;
	unsigned char digest[32];

	if (v->blocks < 2 || v->blocks > 51200 || size != v->bytes ||
	    size > 200U * 1024U * 1024U)
		return -1;
	do {
		count = (count + 127) / 128;
		levels[n++] = count;
	} while (count > 1 && n < 8);
	for (i = n; i > 0; i--) {
		offsets[i - 1] = total;
		total += levels[i - 1];
	}
	if ((size_t)total * 4096 != size)
		return -1;
	count = v->blocks;
	for (i = 0; i < n; i++) {
		for (j = 0; j < count; j++) {
			hash(v->salt, image + (size_t)(input + j) * 4096, digest);
			if (memcmp(digest, image + (size_t)offsets[i] * 4096 + j * 32, 32))
				return -1;
		}
		input = offsets[i];
		count = levels[i];
	}
	hash(v->salt, image + (size_t)input * 4096, digest);
	return memcmp(digest, v->root, 32) ? -1 : 0;
}
#endif

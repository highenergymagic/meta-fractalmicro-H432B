/* SPDX-License-Identifier: MIT */
/* Bounded Android sparse v1 decoder, shared with native contract tests. */
#ifndef H432B_SPARSE_IMAGE_H
#define H432B_SPARSE_IMAGE_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define H432B_SPARSE_RAW 0xcac1
#define H432B_SPARSE_FILL 0xcac2
#define H432B_SPARSE_SKIP 0xcac3

struct h432b_sparse {
	uint32_t bytes;
	uint32_t first;
	uint32_t end;
	int gaps;
	const unsigned char *prefix;
};

typedef int (*h432b_sparse_emit)(void *context, uint32_t offset,
				uint32_t bytes, unsigned int type,
				const unsigned char *data);

static inline uint32_t h432b_sparse_u32(const unsigned char *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
	       (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static inline unsigned int h432b_sparse_u16(const unsigned char *p)
{
	return p[0] | (unsigned int)p[1] << 8;
}

static inline int h432b_is_sparse(const void *buffer, size_t size)
{
	return size >= 4 && h432b_sparse_u32(buffer) == 0xed26ff3a;
}

/* Validate the entire download before calling any storage callback. SKIP is
 * an unwritten range, never a request to erase earlier resparse fragments.
 * CRC-bearing encodings are rejected, not accepted with unchecked checksums.
 * The standard fastboot client emits checksum-free v1 fragments.
 */
static inline int h432b_sparse_walk(const void *buffer, size_t size,
				    uint32_t limit, struct h432b_sparse *view,
				    h432b_sparse_emit emit, void *context)
{
	const unsigned char *p = buffer, *data;
	uint32_t blocks, chunks, position, bytes, payload, total, i;
	unsigned int file_header, chunk_header, type, pass;
	size_t offset;

	if (size < 28 || !h432b_is_sparse(buffer, size) ||
	    h432b_sparse_u16(p + 4) != 1 || h432b_sparse_u16(p + 6) ||
	    h432b_sparse_u32(p + 12) != 4096 || h432b_sparse_u32(p + 24))
		return -1;
	file_header = h432b_sparse_u16(p + 8);
	chunk_header = h432b_sparse_u16(p + 10);
	blocks = h432b_sparse_u32(p + 16);
	chunks = h432b_sparse_u32(p + 20);
	if (file_header < 28 || file_header > size || chunk_header < 12 ||
	    !blocks || blocks > limit / 4096 || !chunks ||
	    chunks > (size - file_header) / chunk_header)
		return -1;
	memset(view, 0, sizeof(*view));
	view->bytes = blocks * 4096;
	view->first = view->bytes;
	for (pass = 0; pass < (emit ? 2U : 1U); pass++) {
		offset = file_header;
		position = 0;
		for (i = 0; i < chunks; i++) {
			if (size - offset < chunk_header)
				return -1;
			type = h432b_sparse_u16(p + offset);
			blocks = h432b_sparse_u32(p + offset + 4);
			total = h432b_sparse_u32(p + offset + 8);
			if (!blocks || blocks > (view->bytes - position) / 4096 ||
			    total < chunk_header || total > size - offset)
				return -1;
			bytes = blocks * 4096;
			payload = total - chunk_header;
			data = p + offset + chunk_header;
			if ((type == H432B_SPARSE_RAW && payload != bytes) ||
			    (type == H432B_SPARSE_FILL && payload != 4) ||
			    (type == H432B_SPARSE_SKIP && payload) ||
			    (type != H432B_SPARSE_RAW && type != H432B_SPARSE_FILL &&
			     type != H432B_SPARSE_SKIP))
				return -1;
			if (type != H432B_SPARSE_SKIP) {
				if (!pass) {
					if (view->first == view->bytes)
						view->first = position;
					else if (position != view->end)
						view->gaps = 1;
					view->end = position + bytes;
					if (!position && type == H432B_SPARSE_RAW)
						view->prefix = data;
				} else if (emit(context, position, bytes, type, data)) {
					return -1;
				}
			}
			position += bytes;
			offset += total;
		}
		if (offset != size || position != view->bytes || !view->end)
			return -1;
	}
	return 0;
}

/* Static UBI updates are sequential. Android resparse of a fully defined
 * raw image uses a skipped prefix/suffix around ordered RAW/FILL extents.
 * Reject holes and replay/out-of-order pieces rather than inventing data.
 */
static inline int h432b_sparse_next(const struct h432b_sparse *view,
				    uint32_t cursor, uint32_t total)
{
	return view->gaps || view->first != cursor ||
	       (cursor && view->bytes != total) ? -1 : 0;
}

#endif

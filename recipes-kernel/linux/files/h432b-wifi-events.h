/* SPDX-License-Identifier: GPL-2.0-only */
/* One-shot C2H FIFO inspection using the factory cumulative block protocol. */
#define WIFI_C2H_COUNT 0x48
#define WIFI_C2H_FIFO 0x18e80
#define WIFI_EVENT_MAX 16384

struct h432b_event_result {
	bool attempted;
	int error;
	int cleanup;
	u16 baseline, blocks, after, status, final_status;
	unsigned int bytes, events;
	u8 first_code, first_seq;
	u16 first_length;
	u8 head[64];
};

static int wifi_event_parse(const u8 *data, unsigned int size,
			    struct h432b_event_result *r)
{
	unsigned int offset = 0, packet, payload, stride;

	while (offset < size) {
		if (size - offset < 32 ||
		    (get_unaligned_le16(data + offset + 4) & 0x1ff) != 0x1ff)
			return -EBADMSG;
		packet = get_unaligned_le16(data + offset) & 0x3fff;
		payload = get_unaligned_le16(data + offset + 24);
		if (packet < 8 || packet > size - offset - 24 ||
		    payload > packet - 8)
			return -EMSGSIZE;
		stride = ALIGN(packet + 24, 512);
		if (stride > size - offset)
			return -EMSGSIZE;
		if (!r->events) {
			r->first_code = data[offset + 26];
			r->first_seq = data[offset + 27] & 0x7f;
			r->first_length = payload;
		}
		r->events++;
		offset += stride;
	}
	return 0;
}

#ifdef CONFIG_H432B_WIFI_DIAGNOSTICS
static int wifi_event_test(struct sdio_func *func, struct h432b_event_result *r,
			   u16 baseline)
{
	struct mmc_host *host = func->card->host;
	unsigned int saved = func->cur_blksize, pending;
	bool enabled = false, block_attempted = false;
	u8 *data, ioex;
	u16 mask;
	int error = 0, restore;

	if (!saved || host->max_blk_count < WIFI_EVENT_MAX / 512 ||
	    host->max_req_size < WIFI_EVENT_MAX || host->max_seg_size < WIFI_EVENT_MAX)
		return -EOPNOTSUPP;
	data = kzalloc(WIFI_EVENT_MAX, GFP_KERNEL);
	if (!data)
		return -ENOMEM;
	r->baseline = baseline;
	sdio_claim_host(func);
	ioex = sdio_f0_readb(func, SDIO_CCCR_IOEx, &error);
	if (error)
		goto out;
	if (!(ioex & BIT(func->num))) {
		enabled = true;
		error = sdio_enable_func(func);
		if (error)
			goto out;
	}
	mask = sdio_readw(func, WIFI_HIMR, &error);
	if (error)
		goto out;
	if (mask || func->irq_handler) {
		error = -EBUSY;
		goto out;
	}
	r->status = sdio_readw(func, WIFI_HISR, &error);
	if (error)
		goto out;
	r->blocks = sdio_readw(func, WIFI_C2H_COUNT, &error);
	if (error)
		goto out;
	pending = (u16)(r->blocks - baseline);
	if (!pending) {
		error = -ENODATA;
		goto out;
	}
	if (pending > WIFI_EVENT_MAX / 512) {
		error = -EOVERFLOW;
		goto out;
	}
	block_attempted = true;
	error = sdio_set_block_size(func, 512);
	if (error)
		goto out;
	/* First C2H FIFO read since chip startup: its two-bit sequence is zero.
	 * One CMD53 per request; do not split and increment the port sequence.
	 */
	error = wifi_sdio_blocks(func, false, WIFI_C2H_FIFO, data, pending);
	if (error)
		goto out;
	r->bytes = pending * 512;
	memcpy(r->head, data, sizeof(r->head));
	error = wifi_event_parse(data, r->bytes, r);
	r->after = sdio_readw(func, WIFI_C2H_COUNT, &restore);
	if (!r->cleanup)
		r->cleanup = restore;
	r->final_status = sdio_readw(func, WIFI_HISR, &restore);
	if (!r->cleanup)
		r->cleanup = restore;
out:
	if (block_attempted) {
		restore = sdio_set_block_size(func, saved);
		if (!r->cleanup)
			r->cleanup = restore;
	}
	if (enabled) {
		restore = sdio_disable_func(func);
		if (!r->cleanup)
			r->cleanup = restore;
	}
	sdio_release_host(func);
	kfree(data);
	return error;
}
#endif

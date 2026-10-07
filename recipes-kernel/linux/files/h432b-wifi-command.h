/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Bounded RTL8712 H2C/C2H loopback qualification.
 * Loopback fields/protocol: Realtek GPL-2.0 rtl871x_cmd.h, commit
 * 2237e98dacd8421b38beb2d1aad88aa2b9f79dd8.
 * SDIO descriptor, FIFO and byte-mode command transport match the factory HAL.
 */
#include "sdio_ops.h"

struct h432b_command_result {
	bool attempted, sent, matched;
	int error, cleanup;
	unsigned int batches, events, debug_events, bytes, replies, reply_length;
	u8 command_seq;
	u16 consumed, status_before, status_after;
	u8 port_seq, event_seq, public_pages, command_pages;
	u8 reply[28];
	u8 debug_head[32];
};

static void wifi_loopback_packet(u8 *packet, u8 seq)
{
	u8 *p = packet + 40;

	memset(packet, 0, 512);
	put_unaligned_le32(0x8c200028, packet); /* OWN/FSG/LSG, offset32, length40 */
	put_unaligned_le32(0x1300, packet + 4); /* command queue */
	put_unaligned_le32(0x002c0020 | ((u32)seq << 24), packet + 32);
	p[0] = 2; p[1] = 0x11 + seq * 0x10; p[2] = 0x33 + seq * 0x10;
	p[3] = 1; p[4] = 0x65; p[5] = 0x87; /* request one loopback event */
	put_unaligned_le16(0x1234, p + 6);
	put_unaligned_le16(0x5678, p + 8);
	put_unaligned_le32(0x12345678, p + 12);
	p[16] = 0x9a;
	put_unaligned_le16(0x2468, p + 18);
	p[20] = 0xbc;
	put_unaligned_le32(0x89abcdef, p + 24);
}

/* Factory firmware reply observed on hardware: command header + four bytes.
 * Require the exact length, sequence and distinct request tags. This differs
 * from the transformed 28-byte response documented in the vendor header.
 */
static bool wifi_loopback_matches(const u8 *p, unsigned int length, u8 seq)
{
	return length == 12 &&
		get_unaligned_le32(p) == (0x002c0020 | ((u32)seq << 24)) &&
		get_unaligned_le32(p + 4) == 0 &&
		p[8] == 2 && p[9] == 0x11 + seq * 0x10 &&
		p[10] == 0x33 + seq * 0x10 && p[11] == 1;
}

/* Host held. Returns 1 for a consumed batch, 0 for empty, or negative errno. */
static int wifi_command_drain(struct sdio_func *func,
			      struct h432b_command_result *r, u8 *data)
{
	struct h432b_event_result parsed = {};
	unsigned int pending, offset, packet, length;
	u16 count;
	u8 code, seq;
	int error = 0;

	count = sdio_readw(func, WIFI_C2H_COUNT, &error);
	if (error)
		return error;
	pending = (u16)(count - r->consumed);
	if (!pending)
		return 0;
	if (pending > WIFI_EVENT_MAX / 512 || r->batches >= 64)
		return -EOVERFLOW;
	error = sdio_memcpy_fromio(func, data, WIFI_C2H_FIFO | (r->port_seq & 3),
				  pending * 512);
	if (error)
		return error;
	r->port_seq = (r->port_seq + 1) & 3;
	r->consumed = count; /* Not a later count: new events can arrive during read. */
	r->batches++;
	r->bytes += pending * 512;
	error = wifi_event_parse(data, pending * 512, &parsed);
	if (error)
		return error;
	for (offset = 0; offset < pending * 512; offset += ALIGN(packet + 24, 512)) {
		packet = get_unaligned_le16(data + offset) & 0x3fff;
		length = get_unaligned_le16(data + offset + 24);
		code = data[offset + 26];
		seq = data[offset + 27] & 0x7f;
		if (seq != r->event_seq)
			return -EILSEQ;
		r->event_seq = (r->event_seq + 1) & 0x7f;
		r->events++;
		if (code == 19) {
			r->debug_events++;
			memset(r->debug_head, 0, sizeof(r->debug_head));
			memcpy(r->debug_head, data + offset + 32,
			       min_t(unsigned int, length, sizeof(r->debug_head)));
		}
		if (code == 18) {
			if (!r->sent || r->matched)
				return -EPROTO;
			r->reply_length = length;
			memset(r->reply, 0, sizeof(r->reply));
			memcpy(r->reply, data + offset + 32,
			       min_t(unsigned int, length, sizeof(r->reply)));
			if (!wifi_loopback_matches(data + offset + 32, length, r->command_seq))
				return -EBADMSG;
			r->matched = true;
			r->replies++;
		}
	}
	return 1;
}

static int wifi_command_test(struct sdio_func *func,
			     struct h432b_command_result *r, u16 baseline)
{
	struct mmc_host *host = func->card->host;
	unsigned int saved = func->cur_blksize, i;
	bool enabled = false, blocks = false;
	u8 *data, *packet, ioex;
	int error = 0, restore, drain;
	u16 mask;

	if (!saved || host->max_blk_count < WIFI_EVENT_MAX / 512 ||
	    host->max_req_size < WIFI_EVENT_MAX || host->max_seg_size < WIFI_EVENT_MAX)
		return -EOPNOTSUPP;
	data = kzalloc(WIFI_EVENT_MAX + 512, GFP_KERNEL);
	if (!data)
		return -ENOMEM;
	packet = data + WIFI_EVENT_MAX;
	wifi_loopback_packet(packet, 1);
	r->consumed = baseline;
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
	blocks = true;
	error = sdio_set_block_size(func, 512);
	if (error)
		goto out;
	r->status_before = sdio_readw(func, WIFI_HISR, &error);
	if (error)
		goto out;
	/* Drain boot events before sending; every loop has a fixed bound. */
	for (i = 0; i < 32; i++) {
		drain = wifi_command_drain(func, r, data);
		if (drain < 0) {
			error = drain;
			goto out;
		}
		if (!drain)
			break;
	}
	if (i == 32) {
		error = -EBUSY;
		goto out;
	}
	for (r->command_seq = 1; r->command_seq <= 2; r->command_seq++) {
		r->public_pages = sdio_readb(func, 1, &error);
		if (error)
			goto out;
		r->command_pages = sdio_readb(func, 3, &error);
		if (error)
			goto out;
		if (r->command_pages < r->public_pages ||
		    (r->command_pages - r->public_pages <= 2 && r->public_pages <= 5)) {
			error = -ENOSPC;
			goto out;
		}
		wifi_loopback_packet(packet, r->command_seq);
		r->matched = false;
		/* Factory H2C is one 512-byte, incrementing, BYTE-mode CMD53.
		 * blocks=0 explicitly avoids the generic helper selecting block mode.
		 */
		error = mmc_io_rw_extended(func->card, 1, func->num, 0x18c80, 1,
					  packet, 0, 512);
		if (error)
			goto out;
		r->sent = true;
		for (i = 0; i < 100 && !r->matched; i++) {
			drain = wifi_command_drain(func, r, data);
			if (drain < 0) {
				error = drain;
				goto out;
			}
			if (r->matched)
				break;
			sdio_release_host(func);
			msleep(20);
			sdio_claim_host(func);
		}
		if (!r->matched) {
			error = -ETIMEDOUT;
			goto out;
		}
	}

out:
	if (blocks) {
		r->status_after = sdio_readw(func, WIFI_HISR, &restore);
		r->cleanup = restore;
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

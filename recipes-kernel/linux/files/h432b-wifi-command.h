/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Bounded RTL8712 H2C/C2H loopback qualification.
 * Loopback fields/protocol: Realtek GPL-2.0 rtl871x_cmd.h, commit
 * 2237e98dacd8421b38beb2d1aad88aa2b9f79dd8.
 * SDIO descriptor, FIFO and block-mode command transport match the factory HAL.
 */
#include "sdio_ops.h"

/* Non-atomic, ordered snapshots; reads do not acknowledge or drain RX. */
struct h432b_command_snapshot {
	u8 phase, tx_ctrl, public_pages, command_pages, errors[3];
	u16 status, rx_blocks, c2h_blocks;
	int error;
};

struct h432b_command_result {
	bool attempted, sent, matched, opmode;
	int error, cleanup;
	unsigned int batches, events, debug_events, bytes, replies, reply_length;
	u8 command_seq;
	u16 consumed, status_before, status_after;
	u8 port_seq, event_seq, public_pages, command_pages;
	u8 reply[28];
	u8 debug_head[32];
	unsigned int mac_stage;
	u32 mac_before[2], mac_after[2];
	u8 pmc_after, pause_after, debug_after;
	unsigned int snapshots;
	struct h432b_command_snapshot snapshot[5];
};

/* Host held. Keep failures visible separately from the command result. */
static void wifi_command_snapshot(struct sdio_func *func,
				  struct h432b_command_result *r, u8 phase)
{
	struct h432b_command_snapshot *s;
	unsigned int i;

	if (r->snapshots >= ARRAY_SIZE(r->snapshot))
		return;
	s = &r->snapshot[r->snapshots++];
	s->phase = phase;
	s->status = sdio_readw(func, WIFI_HISR, &s->error);
	if (s->error)
		return;
	s->rx_blocks = sdio_readw(func, 0x40, &s->error);
	if (s->error)
		return;
	s->c2h_blocks = sdio_readw(func, WIFI_C2H_COUNT, &s->error);
	if (s->error)
		return;
	s->tx_ctrl = sdio_readb(func, 0, &s->error);
	if (s->error)
		return;
	s->public_pages = sdio_readb(func, 1, &s->error);
	if (s->error)
		return;
	s->command_pages = sdio_readb(func, 3, &s->error);
	if (s->error)
		return;
	/* Vendor SDIOERR_RPT, CMD_ERRCNT, DATA_ERRCNT; no clear write. */
	for (i = 0; i < ARRAY_SIZE(s->errors); i++) {
		s->errors[i] = sdio_readb(func, 0xc0 + i, &s->error);
		if (s->error)
			return;
	}
}

/*
 * Post-firmware setup from the factory HAL and Realtek GPL hal_init.c.
 * Host held; stop at the first failed operation. Polling owns the queues,
 * so omit factory HIMR=0x000f until a matching interrupt handler exists.
 * These are live initialization writes, not registers to restore afterward.
 */
static int wifi_command_mac_init(struct sdio_func *func,
				 struct h432b_command_result *r)
{
	int error = 0;

	r->mac_before[0] = wifi_read(func, 4, 0x48, &error);
	if (error)
		return error;
	r->mac_stage = 1;
	wifi_write(func, 4, 0x48, r->mac_before[0] | BIT(25), &error);
	if (error)
		return error;
	r->mac_before[1] = wifi_read(func, 4, 0x40, &error);
	if (error)
		return error;
	r->mac_stage = 2;
	wifi_write(func, 4, 0x40, r->mac_before[1] & 0x00ffffff, &error);
	if (error)
		return error;
	r->mac_stage = 3;
	wifi_write(func, 1, 0x06, 0x3b, &error);
	if (error)
		return error;
	r->mac_stage = 4;
	wifi_write(func, 1, 0x40, 0xfc, &error);
	if (error)
		return error;
	r->mac_stage = 5;
	wifi_write(func, 1, 0x42, 0x00, &error);
	if (error)
		return error;
	r->mac_stage = 6;
	sdio_writeb(func, 0, 0xff, &error);
	if (error)
		return error;
	r->mac_after[0] = wifi_read(func, 4, 0x48, &error);
	if (error)
		return error;
	r->mac_after[1] = wifi_read(func, 4, 0x40, &error);
	if (error)
		return error;
	r->pmc_after = wifi_read(func, 1, 0x06, &error);
	if (error)
		return error;
	r->pause_after = wifi_read(func, 1, 0x42, &error);
	if (error)
		return error;
	r->debug_after = sdio_readb(func, 0xff, &error);
	if (error)
		return error;
	if (!(r->mac_after[0] & BIT(25)) ||
	    (r->mac_after[1] & 0xff0000ff) != 0xfc ||
	    r->pause_after || r->debug_after)
		return -EIO;
	/* PMC_FSM+2 is live status, not a write/readback latch. */
	r->mac_stage = 7;
	return 0;
}

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

static void wifi_opmode_packet(u8 *packet, u8 seq)
{
	memset(packet, 0, 512);
	put_unaligned_le32(0x8c200010, packet); /* 8-byte header + padded params */
	put_unaligned_le32(0x1300, packet + 4);
	put_unaligned_le32(0x00110008 | ((u32)seq << 24), packet + 32);
	packet[40] = 1; /* infrastructure mode; no scan or association */
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
	/* Factory FIFO reads use block mode even for exactly one block. */
	error = mmc_io_rw_extended(func->card, 0, func->num,
				  WIFI_C2H_FIFO | (r->port_seq & 3), 1,
				  data, pending, 512);
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
		if (r->opmode && code == 19 && r->sent &&
		    length == sizeof("set opmode: 1\n") &&
		    !memcmp(data + offset + 32, "set opmode: 1\n", length)) {
			if (r->matched)
				return -EPROTO;
			r->reply_length = length;
			memcpy(r->reply, data + offset + 32, length);
			r->matched = true;
			r->replies++;
		}
		if (code == 18) {
			if (r->opmode)
				return -EPROTO;
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
	error = wifi_command_mac_init(func, r);
	if (error)
		goto out;
	wifi_command_snapshot(func, r, 0);
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
	for (r->command_seq = 1; r->command_seq <= (r->opmode ? 16 : 2); r->command_seq++) {
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
		wifi_command_snapshot(func, r, r->command_seq * 2 - 1);
		if (r->opmode)
			wifi_opmode_packet(packet, r->command_seq);
		else
			wifi_loopback_packet(packet, r->command_seq);
		r->matched = false;
		/* Factory H2C uses one 512-byte block, incrementing CMD53.
		 * The interface callback at +0x38 is io_ops +0x18 (block write),
		 * not +0x1c (byte write): intf_hdl embeds io_ops at +0x20.
		 */
		error = mmc_io_rw_extended(func->card, 1, func->num, 0x18c80, 1,
					  packet, 1, 512);
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
		wifi_command_snapshot(func, r, r->command_seq * 2);
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

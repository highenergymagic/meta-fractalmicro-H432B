/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * RTL8712 SDIO receive FIFO. Records contain a 24-byte descriptor, optional
 * driver information in units of eight bytes, then an 802.11 frame. Each
 * record occupies whole 512-byte blocks. RX and C2H have independent counters
 * and two-bit port sequences; a failed transfer must not be silently retried.
 */
#define WIFI_RX_COUNT 0x40
#define WIFI_RX_FIFO 0x18e40
#define WIFI_RX_MAX 49152

struct h432b_rx_result {
	void (*receive)(void *context, const u8 *frame, unsigned int length, u32 descriptor);
	void *context;
	u16 consumed;
	u8 port_seq;
	unsigned int batches, bytes, frames, crc_errors, icv_errors, high_water;
};

static int wifi_rx_parse(const u8 *data, unsigned int size,
			 struct h432b_rx_result *r)
{
	unsigned int offset = 0, length, info, stride;
	u32 descriptor;

	while (offset < size) {
		if (size - offset < 24)
			return -EMSGSIZE;
		descriptor = get_unaligned_le32(data + offset);
		length = descriptor & 0x3fff;
		info = ((descriptor >> 16) & 0xf) * 8;
		if (!length || info + 24 > size - offset ||
		    length > size - offset - 24 - info)
			return -EMSGSIZE;
		stride = ALIGN(24 + info + length, 512);
		if (stride > size - offset)
			return -EMSGSIZE;
		if (descriptor & BIT(14))
			r->crc_errors++;
		else if (descriptor & BIT(15))
			r->icv_errors++;
		else {
			r->frames++;
			if (r->receive)
				r->receive(r->context, data + offset + 24 + info, length, descriptor);
		}
		/* Association and controlled-port delivery are not implemented yet.
		 * Never feed unassociated frames or raw 802.11 bytes to Ethernet.
		 */
		offset += stride;
	}
	return 0;
}

/* Host held. Read only the captured count; arrivals during this transfer stay
 * pending. Exactly one block-mode CMD53 consumes one port sequence value.
 */
static int wifi_rx_drain(struct sdio_func *func, struct h432b_rx_result *r,
			 u8 *data)
{
	u16 count;
	unsigned int pending;
	int error = 0;

	count = sdio_readw(func, WIFI_RX_COUNT, &error);
	if (error)
		return error;
	pending = (u16)(count - r->consumed);
	if (!pending)
		return 0;
	r->high_water = max(r->high_water, pending);
	if (pending > WIFI_RX_MAX / 512)
		return -EOVERFLOW;
	error = mmc_io_rw_extended(func->card, 0, func->num,
				  WIFI_RX_FIFO | (r->port_seq & 3), 1,
				  data, pending, 512);
	if (error)
		return error;
	r->port_seq = (r->port_seq + 1) & 3;
	r->consumed = count;
	r->batches++;
	r->bytes += pending * 512;
	error = wifi_rx_parse(data, pending * 512, r);
	return error ? error : 1;
}

/* SPDX-License-Identifier: GPL-2.0-only */
/* RTL8712 SDIO command framing. Callers hold the owner mutex and MMC host.
 * A failed CMD53 has unknown consumption: never retry it or reuse its sequence.
 */
#define WIFI_H2C_MAX 884

static unsigned int wifi_h2c_packet(u8 *packet, u8 code, u8 sequence,
				    const void *parameters, unsigned int length)
{
	unsigned int padded = ALIGN(length, 8);
	unsigned int transfer = ALIGN(40 + padded, 512);

	memset(packet, 0, transfer);
	put_unaligned_le32(0x8c200000 | (8 + padded), packet);
	put_unaligned_le32(0x1300, packet + 4);
	put_unaligned_le32(padded | ((u32)code << 16) |
			   ((u32)(sequence & 0x7f) << 24), packet + 32);
	memcpy(packet + 40, parameters, length);
	return transfer;
}

static int wifi_h2c_send(struct sdio_func *func, struct h432b_command_result *r,
			 u8 code, const void *parameters, unsigned int length)
{
	u8 *packet, public, command;
	unsigned int transfer, pages, tries;
	int error = 0;

	if (!length || length > WIFI_H2C_MAX || !r->irq_owned ||
	    READ_ONCE(r->cancelled))
		return -EINVAL;
	packet = kzalloc(1024, GFP_KERNEL);
	if (!packet)
		return -ENOMEM;
	transfer = wifi_h2c_packet(packet, code, r->next_command, parameters, length);
	pages = DIV_ROUND_UP(40 + ALIGN(length, 8), 128);
	for (tries = 0; tries < 100; tries++) {
		public = wifi_sdio_readb(func, 1, &error);
		if (error)
			goto out;
		command = wifi_sdio_readb(func, 3, &error);
		if (error)
			goto out;
		if (command >= public &&
		    (command - public > pages || public > pages + 3))
			break;
		sdio_release_host(func);
		usleep_range(1000, 2000);
		sdio_claim_host(func);
	}
	if (tries == 100) {
		error = -ENOSPC;
		goto out;
	}
	/* Asynchronous events, not debug strings, acknowledge normal commands. */
	r->sent = false;
	error = mmc_io_rw_extended(func->card, 1, func->num, 0x18c80, 1,
				  packet, transfer / 512, 512);
	if (!error) {
		r->next_command = (r->next_command + 1) & 0x7f;
		r->commands_done++;
	}
out:
	kfree_sensitive(packet);
	return error;
}

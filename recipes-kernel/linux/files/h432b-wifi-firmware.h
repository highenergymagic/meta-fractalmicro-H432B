/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * H432B RTL8712 SDIO firmware-start qualification.
 * Protocol reconstructed from the factory SDIO driver. The RTL8712 USB
 * staging driver documents the same descriptor and TCR bit meanings.
 * No vendor firmware bytes are distributed with this implementation.
 */
#include <linux/firmware.h>
#include <linux/mmc/card.h>
#include <linux/mmc/host.h>
#include <linux/unaligned.h>

#include "sdio_ops.h"

#define WIFI_FW_NAME "h432b/rtl8712s.bin"
#define WIFI_FW_CHUNK 49152
#define WIFI_FW_PACKET ALIGN(WIFI_FW_CHUNK + 32, 512)
#define WIFI_FW_FIFO 0x18d40

struct h432b_fw_result {
	bool attempted;
	int error;
	int cleanup;
	unsigned int stage;
	unsigned int bytes;
	unsigned int packets;
	u16 version;
	u16 initial;
	u16 c2h_base;
	u16 imem;
	u16 emem;
	u16 cpu;
	u16 dmem;
	u16 ready;
	u8 boot_source;
};

static int wifi_fw_validate(const struct firmware *fw, u32 *imem, u32 *emem)
{
	u32 dmem;

	if (fw->size < 80 || fw->size > 200000)
		return -EINVAL;
	if (get_unaligned_le16(fw->data) != 0x8712 ||
	    get_unaligned_le32(fw->data + 16) != 48)
		return -EINVAL;
	dmem = get_unaligned_le32(fw->data + 4);
	*imem = get_unaligned_le32(fw->data + 8);
	*emem = get_unaligned_le32(fw->data + 12);
	/* Bound every operand before adding: malformed files cannot wrap sizes. */
	if (!*imem || !*emem || *imem > 65536 || *emem > 65536 || dmem > 65536)
		return -EINVAL;
	if ((size_t)80 + *imem + *emem + dmem != fw->size)
		return -EINVAL;
	return 0;
}

static int wifi_fw_section(struct sdio_func *func, struct h432b_fw_result *r,
			   u8 *packet, const u8 *payload, u32 remaining)
{
	unsigned int length, transfer;
	int error;

	while (remaining) {
		length = min_t(u32, remaining, WIFI_FW_CHUNK);
		transfer = ALIGN(length + 32, 512);
		/* Initialize padding too; never send uninitialized heap contents. */
		memset(packet, 0, transfer);
		put_unaligned_le32(length | (length == remaining ? BIT(28) : 0),
				   packet);
		memcpy(packet + 32, payload, length);
		/*
		 * Factory CMD53 has WRITE, BLOCK_MODE and INCREMENT all set.
		 * Each firmware packet starts at the same FIFO address.
		 */
		error = mmc_io_rw_extended(func->card, 1, func->num,
					  WIFI_FW_FIFO, 1, packet, transfer / 512, 512);
		if (error)
			return error;
		r->bytes += length;
		r->packets++;
		payload += length;
		remaining -= length;
	}
	return 0;
}

static int wifi_fw_poll(struct sdio_func *func, u16 done, u16 check,
			u16 *status, unsigned int tries, unsigned int delay_us)
{
	unsigned int i;
	int error = 0;

	for (i = 0; i <= tries; i++) {
		*status = wifi_read(func, 2, 0x44, &error);
		if (error)
			return error;
		if (*status & done)
			return (*status & check) == check ? 0 : -EBADMSG;
		if (i != tries)
			usleep_range(delay_us, delay_us + 100);
	}
	return -ETIMEDOUT;
}

static int wifi_fw_cpu_bit(struct sdio_func *func, unsigned int offset)
{
	int error = 0;
	u8 wanted, actual;

	wanted = wifi_read(func, 1, offset, &error);
	if (error)
		return error;
	wanted |= BIT(2);
	wifi_write(func, 1, offset, wanted, &error);
	if (error)
		return error;
	actual = wifi_read(func, 1, offset, &error);
	if (error)
		return error;
	return actual == wanted ? 0 : -EIO;
}

/*
 * Factory SDIO configuration, including the board registry overrides.
 * Unspecified bytes remain zero. This is not the USB configuration carried
 * inside the firmware file, nor the trailing file DMEM data.
 */
static void wifi_fw_config(u8 config[48])
{
	memset(config, 0, 48);
	config[2] = 0x14; /* SDIO host interface */
	config[6] = 0x12; /* RFConfig 1: factory 1T2R selection */
	config[0x0e] = 1; /* turbo mode */
	config[0x12] = 2; /* virtual carrier sense: automatic */
	config[0x13] = 2; /* carrier sense type: CTS-to-self */
	config[0x19] = 1; /* factory 40 MHz capability */
	/* MPMode, LowPower and PowerGain are all zero in the board registry. */
}

static int wifi_fw_memory(struct sdio_func *func, const struct firmware *fw,
			  struct h432b_fw_result *r, u32 imem, u32 emem, bool full)
{
	struct mmc_host *host = func->card->host;
	unsigned int saved_blksize = func->cur_blksize;
	bool enable_attempted = false, block_attempted = false;
	u8 *packet, ioex, config[48];
	int error = 0, restore;

	/* Do not silently split an incrementing-address packet across CMD53s. */
	if (!(func->card->cccr.multi_block) || !saved_blksize ||
	    host->max_blk_count < WIFI_FW_PACKET / 512 ||
	    host->max_req_size < WIFI_FW_PACKET || host->max_seg_size < WIFI_FW_PACKET)
		return -EOPNOTSUPP;
	packet = kmalloc(WIFI_FW_PACKET, GFP_KERNEL);
	if (!packet)
		return -ENOMEM;
	sdio_claim_host(func);
	r->stage = 1;
	ioex = sdio_f0_readb(func, SDIO_CCCR_IOEx, &error);
	if (error)
		goto out;
	if (!(ioex & BIT(func->num))) {
		enable_attempted = true;
		error = sdio_enable_func(func);
		if (error)
			goto out;
	}
	block_attempted = true;
	error = sdio_set_block_size(func, 512);
	if (error)
		goto out;
	r->c2h_base = sdio_readw(func, 0x48, &error);
	if (error)
		goto out;
	r->initial = wifi_read(func, 2, 0x44, &error);
	if (error)
		goto out;
	/* Check-result bits 1/3 power up set. Only DONE/READY flags imply stale state. */
	if (r->initial & 0x35) {
		error = -EALREADY;
		goto out;
	}
	r->stage = 2;
	error = wifi_fw_section(func, r, packet, fw->data + 80, imem);
	if (error)
		goto out;
	r->stage = 3;
	error = wifi_fw_poll(func, BIT(0), BIT(1), &r->imem, 100, 1000);
	if (error)
		goto out;
	r->stage = 4;
	error = wifi_fw_section(func, r, packet, fw->data + 80 + imem, emem);
	if (error)
		goto out;
	r->stage = 5;
	error = wifi_fw_poll(func, BIT(2), BIT(3), &r->emem, 100, 1000);
	if (error)
		goto out;
	r->stage = 6;
	error = wifi_fw_cpu_bit(func, 0x08);
	if (error)
		goto out;
	error = wifi_fw_cpu_bit(func, 0x03);
	if (error)
		goto out;
	r->stage = 7;
	error = wifi_fw_poll(func, BIT(5), BIT(5), &r->cpu, 200, 1000);
	if (error)
		goto out;
	r->stage = 8;
	if (!full)
		goto out;
	wifi_fw_config(config);
	r->stage = 9;
	error = wifi_fw_section(func, r, packet, config, sizeof(config));
	if (error)
		goto out;
	r->stage = 10;
	error = wifi_fw_poll(func, BIT(4), BIT(4), &r->dmem, 100, 1000);
	if (error)
		goto out;
	r->stage = 11;
	r->boot_source = wifi_read(func, 1, 0x0a, &error);
	if (error)
		goto out;
	error = wifi_fw_poll(func, BIT(7), BIT(7), &r->ready,
			     r->boot_source & BIT(4) ? 60 : 30, 100000);
	if (!error)
		r->stage = 12; /* Full firmware ready; no host radio command sent. */
out:
	if (block_attempted)
		r->cleanup = sdio_set_block_size(func, saved_blksize);
	if (enable_attempted) {
		restore = sdio_disable_func(func);
		if (!r->cleanup)
			r->cleanup = restore;
	}
	sdio_release_host(func);
	kfree(packet);
	return error;
}

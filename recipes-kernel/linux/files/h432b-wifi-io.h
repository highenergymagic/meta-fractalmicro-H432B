/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * RTL8712S FIFOs require one incrementing, block-mode CMD53 per packet,
 * including 512-byte packets. sdio_memcpy_*() may choose byte mode for that
 * size and split larger transfers. Keep this transport in the wireless driver
 * and use the public MMC request API, not private MMC core declarations.
 *
 * CMD53 encoding and R5 handling follow drivers/mmc/core/sdio_ops.c:
 * Copyright 2006-2007 Pierre Ossman.
 */
#include <linux/mmc/core.h>
#include <linux/scatterlist.h>

static int wifi_sdio_blocks(struct sdio_func *func, bool write,
			    unsigned int address, void *buffer,
			    unsigned int blocks)
{
	struct mmc_host *host = func->card->host;
	struct mmc_request request = {};
	struct mmc_command command = {};
	struct mmc_data data = {};
	struct scatterlist sg, *entry;
	struct sg_table table;
	unsigned int bytes, segments, left, i;
	int error;

	/* Reject before issuing I/O: this FIFO cannot tolerate split requests. */
	if (!blocks || blocks > 511 || address > 0x1ffff ||
	    func->cur_blksize != 512 || !func->card->cccr.multi_block)
		return -EINVAL;
	bytes = blocks * 512;
	if (blocks > host->max_blk_count || bytes > host->max_req_size ||
	    !host->max_seg_size || !host->max_segs ||
	    host->max_blk_size < 512)
		return -EMSGSIZE;
	segments = DIV_ROUND_UP(bytes, host->max_seg_size);
	if (segments > host->max_segs)
		return -EMSGSIZE;

	command.opcode = SD_IO_RW_EXTENDED;
	command.arg = (write ? BIT(31) : 0) | (func->num << 28) |
		BIT(27) | BIT(26) | (address << 9) | blocks;
	command.flags = MMC_RSP_SPI_R5 | MMC_RSP_R5 | MMC_CMD_ADTC;
	data.blksz = 512;
	data.blocks = blocks;
	data.flags = write ? MMC_DATA_WRITE : MMC_DATA_READ;
	/* DMA segments are not separate SDIO requests. Preserve one CMD53
	 * while respecting the host's (often 64 KiB) per-segment limit.
	 */
	if (segments == 1) {
		sg_init_one(&sg, buffer, bytes);
		data.sg = &sg;
	} else {
		error = sg_alloc_table(&table, segments, GFP_KERNEL);
		if (error)
			return error;
		data.sg = table.sgl;
		left = bytes;
		for_each_sg(data.sg, entry, segments, i) {
			unsigned int size = min(left, host->max_seg_size);

			sg_set_buf(entry, (u8 *)buffer + bytes - left, size);
			left -= size;
		}
	}
	data.sg_len = segments;
	request.cmd = &command;
	request.data = &data;
	mmc_set_data_timeout(&data, func->card);
	/* No retries: a failed FIFO request may have consumed part of a packet. */
	mmc_wait_for_req(host, &request);
	if (command.error)
		error = command.error;
	else if (data.error)
		error = data.error;
	else if (!mmc_host_is_spi(host) && (command.resp[0] & R5_ERROR))
		error = -EIO;
	else if (!mmc_host_is_spi(host) && (command.resp[0] & R5_FUNCTION_NUMBER))
		error = -EINVAL;
	else if (!mmc_host_is_spi(host) && (command.resp[0] & R5_OUT_OF_RANGE))
		error = -ERANGE;
	else
		error = data.bytes_xfered == bytes ? 0 : -EIO;
	if (segments > 1)
		sg_free_table(&table);
	return error;
}

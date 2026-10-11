# SPDX-License-Identifier: MIT
"""Execute the actual single-request CMD53 helper with mocked MMC submission."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

FILES = Path(__file__).resolve().parents[1] / "recipes-kernel/linux/files"


class WifiIO(unittest.TestCase):
    @unittest.skipUnless(os.environ.get("WIFI_RX_NATIVE_CC"), "requires pinned compiler")
    def test_native_cmd53_vectors(self):
        helper = (FILES / "h432b-wifi-io.h").read_text()
        helper = "\n".join(line for line in helper.splitlines()
                           if not line.startswith("#include"))
        source = r"""
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
typedef uint8_t u8;
#define BIT(n) (1U << (n))
#define DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))
#define min(a, b) ((a) < (b) ? (a) : (b))
#define GFP_KERNEL 0
#define SD_IO_RW_EXTENDED 53
#define MMC_RSP_SPI_R5 1
#define MMC_RSP_R5 2
#define MMC_CMD_ADTC 4
#define MMC_DATA_WRITE 1
#define MMC_DATA_READ 2
#define R5_ERROR BIT(11)
#define R5_FUNCTION_NUMBER BIT(9)
#define R5_OUT_OF_RANGE BIT(8)
struct mmc_host { unsigned max_blk_count, max_req_size, max_seg_size, max_segs, max_blk_size; };
struct mmc_card { struct mmc_host *host; struct { bool multi_block; } cccr; };
struct sdio_func { struct mmc_card *card; unsigned cur_blksize, num; };
struct scatterlist { void *p; unsigned size; };
struct sg_table { struct scatterlist *sgl; };
#define for_each_sg(head, entry, count, index) \
    for ((index) = 0, (entry) = (head); (index) < (count); (index)++, (entry)++)
struct mmc_command { unsigned opcode, arg, flags, resp[4]; int error; };
struct mmc_data { unsigned blksz, blocks, flags, sg_len, bytes_xfered; struct scatterlist *sg; int error; };
struct mmc_request { struct mmc_command *cmd; struct mmc_data *data; };
static unsigned calls, expected_argument, response, short_count, allocations;
static int command_error, data_error, allocation_error;
static int sg_alloc_table(struct sg_table *t, unsigned n, int flags) {
    (void)flags;
    if (allocation_error) return allocation_error;
    t->sgl = calloc(n, sizeof(*t->sgl)); allocations++; return 0;
}
static void sg_free_table(struct sg_table *t) { free(t->sgl); allocations--; }
static void sg_set_buf(struct scatterlist *sg, void *p, unsigned size) {
    sg->p = p; sg->size = size;
}
static void sg_init_one(struct scatterlist *sg, void *p, unsigned size) {
    sg->p = p; sg->size = size;
}
static void mmc_set_data_timeout(struct mmc_data *data, struct mmc_card *card) {
    (void)data; (void)card;
}
static bool mmc_host_is_spi(struct mmc_host *host) { (void)host; return false; }
static void mmc_wait_for_req(struct mmc_host *host, struct mmc_request *request) {
    unsigned total = 0;
    calls++;
    assert(request->cmd->opcode == 53);
    assert(request->cmd->arg == expected_argument);
    assert(request->data->sg_len <= host->max_segs);
    assert(request->data->blksz == 512);
    for (unsigned i = 0; i < request->data->sg_len; i++) {
        assert(request->data->sg[i].size <= host->max_seg_size);
        assert(request->data->sg[i].p == (u8 *)request->data->sg[0].p + total);
        total += request->data->sg[i].size;
    }
    assert(total == 512 * request->data->blocks);
    request->cmd->error = command_error;
    request->cmd->resp[0] = response;
    request->data->error = data_error;
    request->data->bytes_xfered = 512 * request->data->blocks - short_count;
}
""" + helper + r"""
int main(void) {
    struct mmc_host host = { 511, 65536, 65536, 1, 512 };
    struct mmc_card card = { .host = &host, .cccr.multi_block = true };
    struct sdio_func func = { .card = &card, .cur_blksize = 512, .num = 1 };
    unsigned char buffer[131072];
    expected_argument = 0x9f190001;
    assert(!wifi_sdio_blocks(&func, true, 0x18c80, buffer, 1));
    assert(calls == 1);
    expected_argument = 0x1f1d0002;
    assert(!wifi_sdio_blocks(&func, false, 0x18e80, buffer, 2));
    assert(calls == 2);
    assert(wifi_sdio_blocks(&func, false, 0x18e80, buffer, 0) == -EINVAL);
    assert(wifi_sdio_blocks(&func, false, 0x18e80, buffer, 512) == -EINVAL);
    assert(wifi_sdio_blocks(&func, false, 0x20000, buffer, 1) == -EINVAL);
    host.max_seg_size = 512;
    assert(wifi_sdio_blocks(&func, false, 0x18e80, buffer, 2) == -EMSGSIZE);
    assert(calls == 2); host.max_seg_size = 65536;
    command_error = -ETIMEDOUT;
    assert(wifi_sdio_blocks(&func, false, 0x18e80, buffer, 2) == -ETIMEDOUT);
    assert(calls == 3); command_error = 0;
    data_error = -EILSEQ;
    assert(wifi_sdio_blocks(&func, false, 0x18e80, buffer, 2) == -EILSEQ);
    data_error = 0; response = R5_ERROR;
    assert(wifi_sdio_blocks(&func, false, 0x18e80, buffer, 2) == -EIO);
    response = R5_FUNCTION_NUMBER;
    assert(wifi_sdio_blocks(&func, false, 0x18e80, buffer, 2) == -EINVAL);
    response = R5_OUT_OF_RANGE;
    assert(wifi_sdio_blocks(&func, false, 0x18e80, buffer, 2) == -ERANGE);
    response = 0; short_count = 1;
    assert(wifi_sdio_blocks(&func, false, 0x18e80, buffer, 2) == -EIO);
    assert(calls == 8); /* no transfer was retried */
    short_count = 0;
    host.max_req_size = 131072; host.max_segs = 4; host.max_seg_size = 65535;
    expected_argument = 0x1f1d0100;
    assert(!wifi_sdio_blocks(&func, false, 0x18e80, buffer, 256));
    assert(calls == 9 && !allocations); /* three DMA segments, one CMD53 */
    command_error = -ETIMEDOUT;
    assert(wifi_sdio_blocks(&func, false, 0x18e80, buffer, 256) == -ETIMEDOUT);
    assert(calls == 10 && !allocations); command_error = 0;
    allocation_error = -ENOMEM;
    assert(wifi_sdio_blocks(&func, false, 0x18e80, buffer, 256) == -ENOMEM);
    assert(calls == 10 && !allocations); allocation_error = 0;
    host.max_seg_size = 0;
    assert(wifi_sdio_blocks(&func, false, 0x18e80, buffer, 1) == -EMSGSIZE);
    return 0;
}
"""
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "test.c"
            binary = Path(directory) / "test"
            path.write_text(source)
            subprocess.run([os.environ["WIFI_RX_NATIVE_CC"], "-std=c11", "-Wall",
                            "-Wextra", "-Werror", str(path), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

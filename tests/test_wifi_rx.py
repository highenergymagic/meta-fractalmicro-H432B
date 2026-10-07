# SPDX-License-Identifier: MIT
"""RX wire/lifecycle contracts and optional compiled C parser vectors.

Set WIFI_RX_NATIVE_CC to a native compiler inside the pinned build container
to execute the actual driver parser. This does not emulate SDIO hardware.
"""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
FILES = ROOT / "recipes-kernel/linux/files"
RX = (FILES / "h432b-wifi-rx.h").read_text()
NET = (FILES / "h432b-wifi-net.h").read_text()
CMD = (FILES / "h432b-wifi-command.h").read_text()


class WifiReceive(unittest.TestCase):
    def test_independent_fifo_and_captured_count(self):
        for token in ("WIFI_RX_COUNT 0x40", "WIFI_RX_FIFO 0x18e40",
                      "(u16)(count - r->consumed)", "pending > WIFI_RX_MAX / 512",
                      "WIFI_RX_FIFO | (r->port_seq & 3)", "data, pending, 512"):
            self.assertIn(token, RX)
        call = RX.index("error = mmc_io_rw_extended")
        self.assertLess(call, RX.index("r->consumed = count"))
        self.assertLess(RX.index("if (error)", call), RX.index("r->consumed = count"))
        self.assertNotIn("netif_rx", RX)

    def test_persistent_irq_single_consumer(self):
        self.assertIn("alloc_ordered_workqueue", NET)
        self.assertIn("queue_work(net->workqueue, &net->scan_work)", NET)
        self.assertIn("queue_delayed_work(net->workqueue, &net->event_work", NET)
        self.assertIn("if (r->irq_mode && !r->irq_owned)", CMD)
        self.assertIn("r->irq_owned ? BIT(0) : 0", CMD)
        callback = CMD.split("static void wifi_command_irq", 1)[1].split(
            "static int wifi_command_arm", 1)[0]
        self.assertLess(callback.index("sdio_readw(func, WIFI_HISR"),
                        callback.index("sdio_writew(func, 0, WIFI_HIMR"))
        self.assertIn("actual == mask ? 0 : -EIO", CMD)
        stop = NET.split("static int wifi_net_stop", 1)[1].split(
            "static netdev_tx_t", 1)[0]
        self.assertLess(stop.index("stopping, true"), stop.index("cancel_work_sync"))
        self.assertLess(stop.index("cancel_delayed_work_sync"),
                        stop.index("sdio_release_irq"))
        self.assertIn("sdio_disable_func", stop)
        self.assertIn("r->next_command = 1", NET)
        self.assertIn("r->consumed = owner->firmware.c2h_base", NET)

    @unittest.skipUnless(os.environ.get("WIFI_RX_NATIVE_CC"),
                         "actual C vectors require the pinned container compiler")
    def test_compiled_parser_vectors(self):
        # Compile the actual parser, not a separately maintained Python model.
        parser = RX.split("/* Host held.", 1)[0]
        prefix = r"""
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
#define BIT(n) (1U << (n))
#define ALIGN(n, a) (((n) + (a) - 1) & ~((a) - 1))
static uint32_t get_unaligned_le32(const void *p)
{
    const uint8_t *b = p;
    return b[0] | b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
}
static void put32(uint8_t *b, uint32_t n)
{
    b[0] = n; b[1] = n >> 8; b[2] = n >> 16; b[3] = n >> 24;
}
"""
        vectors = r"""
int main(void)
{
    uint8_t b[2048] = {0};
    struct h432b_rx_result r = {0};
    put32(b, 100);
    assert(wifi_rx_parse(b, 512, &r) == 0 && r.frames == 1);
    memset(&r, 0, sizeof(r));
    put32(b, 480 | (2 << 16)); /* 24+16+480: two blocks, not one */
    assert(wifi_rx_parse(b, 512, &r) == -EMSGSIZE);
    assert(wifi_rx_parse(b, 1024, &r) == 0 && r.frames == 1);
    put32(b + 1024, 24 | BIT(14));
    put32(b + 1536, 24 | BIT(15));
    memset(&r, 0, sizeof(r));
    assert(wifi_rx_parse(b, sizeof(b), &r) == 0);
    assert(r.frames == 1 && r.crc_errors == 1 && r.icv_errors == 1);
    memset(b, 0, sizeof(b));
    assert(wifi_rx_parse(b, 512, &r) == -EMSGSIZE); /* zero length */
    put32(b, 0x3fff);
    assert(wifi_rx_parse(b, 512, &r) == -EMSGSIZE);
    put32(b, 1 | (15 << 16));
    assert(wifi_rx_parse(b, 128, &r) == -EMSGSIZE); /* truncated driver info */
    assert(wifi_rx_parse(b, 23, &r) == -EMSGSIZE); /* truncated descriptor */
    put32(b, 489);
    assert(wifi_rx_parse(b, 513, &r) == -EMSGSIZE); /* missing block padding */
    assert(wifi_rx_parse(b, 0, &r) == 0);
    return 0;
}
"""
        with tempfile.TemporaryDirectory(prefix="h432b-rx-test-") as directory:
            source = Path(directory) / "test.c"
            binary = Path(directory) / "test"
            source.write_text(prefix + parser + vectors)
            subprocess.run([os.environ["WIFI_RX_NATIVE_CC"], "-std=c11",
                            "-Wall", "-Wextra", "-Werror", str(source),
                            "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()

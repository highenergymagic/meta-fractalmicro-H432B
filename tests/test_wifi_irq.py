# SPDX-License-Identifier: MIT
"""Offline acknowledgement lifecycle contracts, not interrupt emulation."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1] / "recipes-kernel/linux"
IRQ = (ROOT / "files/h432b-wifi-irq.h").read_text()
DRIVER = (ROOT / "files/h432b-wifi-transport.c").read_text()
DRIVER += (ROOT / "files/h432b-wifi-debug.h").read_text()


class WifiInterrupt(unittest.TestCase):
    def test_separate_request_requires_full_firmware(self):
        self.assertIn("DEVICE_ATTR_WO(power_ack)", DRIVER)
        self.assertIn("sample->firmware.stage != 12", DRIVER)
        self.assertIn("sample->firmware.cleanup", DRIVER)
        self.assertIn("wifi_ack_test(func, r, h432b_wifi_irq)", DRIVER)

    def test_only_active_state_and_cpwm(self):
        self.assertIn("#define WIFI_CPWM_IRQ BIT(7)", IRQ)
        self.assertIn("| BIT(6) | 0x0c", IRQ)
        self.assertIn("r->old_request ^ BIT(7)", IRQ)
        self.assertIn("sdio_writew(func, WIFI_CPWM_IRQ, WIFI_HIMR", IRQ)
        self.assertNotIn("sdio_memcpy_toio", IRQ)
        self.assertNotIn("sdio_writew(func, 0xffff", IRQ)

    def test_callback_has_no_mutex_or_host_reclaim(self):
        callback = IRQ.split("static void wifi_ack_irq", 1)[1].split(
            "static int wifi_ack_test", 1)[0]
        self.assertNotIn("mutex_lock", callback)
        self.assertNotIn("sdio_claim_host", callback)
        self.assertLess(callback.index("sdio_writew"), callback.index("sdio_readw"))
        self.assertIn("complete(&r->done)", callback)

    def test_wait_releases_host(self):
        wait = IRQ.index("completed = wait_for_completion_timeout")
        self.assertLess(IRQ.rindex("sdio_release_host(func)", 0, wait), wait)
        self.assertIn("msecs_to_jiffies(2000)", IRQ)
        self.assertIn("sdio_claim_host(func)", IRQ[wait:])
        self.assertIn("MMC_CAP_SDIO_IRQ", IRQ)

    def test_no_success_from_stale_reply(self):
        for fragment in ("r->status & WIFI_CPWM_IRQ",
                         "(r->reply ^ r->before) & BIT(7)",
                         "(r->reply & 0x0f) != 0x0c", "-ETIMEDOUT", "-EPROTO"):
            self.assertIn(fragment, IRQ)

    def test_irq_released_before_mask_restored(self):
        self.assertLess(IRQ.index("sdio_release_irq(func)"),
                        IRQ.index("sdio_writew(func, r->saved_mask"))
        self.assertIn("if (irq_attempted)", IRQ)
        self.assertIn("sdio_disable_func(func)", IRQ)
        self.assertIn("func->irq_handler || (ien & BIT(func->num))", IRQ)


if __name__ == "__main__":
    unittest.main()

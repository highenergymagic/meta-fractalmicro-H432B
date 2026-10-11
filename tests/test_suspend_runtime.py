# SPDX-License-Identifier: MIT
"""Static suspend integration contracts; hardware sleep is qualified separately."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
FILES = ROOT / "recipes-kernel/linux/files"

class SuspendRuntime(unittest.TestCase):
    def test_reserved_entry_and_single_wake(self):
        dt = (FILES / "s5pv210-hims-u2-suspend.dtsi").read_text()
        self.assertIn("linux,usable-memory-range = <0x40200000 0x0fe00000>", dt)
        self.assertIn("reg = <0x40020000 0x1000>", dt)
        self.assertIn("wakeup-event-action = <EV_ACT_ASSERTED>", dt)
        self.assertEqual(dt.count("wakeup-source;"), 1)
        self.assertIn("&{/power-keys/power-button}", dt)
        patch = (FILES / "0009-factory-resume-test.patch").read_text()
        self.assertIn("eint_wakeup_mask != (u32)~BIT(22)", patch)
        self.assertIn("refusing sleep with wake mask", patch)

    def test_cell_supply_sequence_and_restore(self):
        code = (FILES / "h432b-braille.c").read_text()
        suspend = code.split("static int display_suspend", 1)[1].split("static int display_resume", 1)[0]
        self.assertLess(suspend.index("shift_frame(h, blank)"), suspend.index("msleep(100)"))
        self.assertLess(suspend.index("msleep(100)"), suspend.index("gpiod_set_value_cansleep(h->enable, 0)"))
        resume = code.split("static int display_resume", 1)[1]
        self.assertLess(resume.index("gpiod_set_value_cansleep(h->enable, 1)"), resume.index("shift_frame(h, h->frame)"))
        self.assertIn("memcpy(h->frame, cells, sizeof(cells))", code)
        self.assertIn("mutex_lock(&h->lock)", suspend)
        self.assertNotIn("writel", code)
        dt = (FILES / "s5pv210-hims-u2-suspend.dtsi").read_text()
        self.assertIn('samsung,pins = "gpj0-3"', dt)
        self.assertNotIn("gpa0", dt)
        self.assertIn("samsung,pin-con-pdn = <0>", dt)

    def test_wifi_sleep_retains_firmware_without_wake(self):
        code = (FILES / "h432b-wifi-transport.c").read_text()
        suspend = code.split("static int h432b_wifi_suspend", 1)[1].split("static int h432b_wifi_resume", 1)[0]
        resume = code.split("static int h432b_wifi_resume", 1)[1]
        self.assertIn("sdio_set_host_pm_flags(func, MMC_PM_KEEP_POWER)", suspend)
        self.assertNotIn("MMC_PM_WAKE_SDIO_IRQ", code)
        self.assertLess(suspend.index("sdio_set_host_pm_flags"), suspend.index("wifi_net_stop"))
        self.assertLess(suspend.index("netif_device_detach"), suspend.index("wifi_net_stop"))
        self.assertIn("rtnl_lock()", suspend)
        self.assertIn("net->faulted", suspend)
        self.assertIn("owner->command.irq_owned", suspend)
        self.assertIn("wifi_net_open(net->dev)", resume)
        self.assertIn("if (!error)", resume)
        self.assertIn("netif_device_attach(net->dev)", resume)
        self.assertIn(".drv.pm = pm_sleep_ptr(&h432b_wifi_pm)", code)
        dt = (FILES / "s5pv210-hims-u2-suspend.dtsi").read_text()
        self.assertIn("&sdhci3", dt)
        self.assertIn("keep-power-in-suspend;", dt)
        self.assertIn('samsung,pins = "gpe0-3"', dt)
        self.assertIn("samsung,pin-con-pdn = <1>", dt)
        self.assertIn("&sd3_bus4 &wifi_power_sleep", dt)

    def test_bluetooth_enable_is_retained_without_uart_wake(self):
        dt = (FILES / "s5pv210-hims-u2-suspend.dtsi").read_text()
        block = dt.split("&bluetooth_enable_pin {", 1)[1].split("};", 1)[0]
        self.assertIn("samsung,pin-con-pdn = <1>", block)
        self.assertIn("samsung,pin-pud-pdn = <0>", block)
        self.assertNotIn("wakeup-source", block)
        bt = (FILES / "s5pv210-hims-u2-bluetooth.dtsi").read_text()
        self.assertIn('samsung,pins = "gpe0-2"', bt)
        self.assertNotIn("wakeup-source", bt)

    def test_deep_sleep_restores_peripheral_controllers(self):
        patch = (FILES / "0021-h432b-suspend-peripheral-retention.patch").read_text()
        self.assertIn("h432b_srom_save[7]", patch)
        self.assertIn("ioremap(0xe8000000, sizeof(h432b_srom_save))", patch)
        self.assertIn("h432b_srom_save[i] = readl", patch)
        self.assertIn("writel(h432b_srom_save[0], h432b_sromc)", patch)
        self.assertIn("SDHCI_QUIRK2_HOST_OFF_CARD_ON", patch)
        self.assertIn('of_machine_is_compatible("hims,braillesense-u2")', patch)
        include = (ROOT / "recipes-kernel/linux/h432b-suspend.inc").read_text()
        self.assertLess(include.index("0009-factory-resume-test.patch"),
                        include.index("0021-h432b-suspend-peripheral-retention.patch"))

    def test_ethernet_sleep_irq_and_loopback_bounds(self):
        rx = (FILES / "0023-smsc911x-bound-loopback-rx.patch").read_text()
        self.assertIn("loopback_rx_pkt[MIN_PACKET_SIZE + 4] __aligned(4)", rx)
        self.assertIn("pktlength != sizeof(pdata->loopback_rx_pkt)", rx)
        self.assertIn("smsc911x_reg_read(pdata, RX_DATA_FIFO)", rx)
        self.assertLess(rx.index("pktlength != sizeof"),
                        rx.rindex("rx_readfifo(pdata"))
        include = (ROOT / "recipes-kernel/linux/h432b-suspend.inc").read_text()
        self.assertNotIn("0022-smsc911x-suspend-irq-order.patch", include)
        self.assertIn("0023-smsc911x-bound-loopback-rx.patch", include)

    def test_ethernet_board_resume_uses_netdev_lifecycle(self):
        patch = (FILES / "0024-h432b-ethernet-full-resume.patch").read_text()
        self.assertIn('of_machine_is_compatible("hims,braillesense-u2")', patch)
        self.assertIn("!device_may_wakeup(dev)", patch)
        self.assertLess(patch.index("dev_close(ndev)"),
                        patch.index("gpiod_set_value_cansleep"))
        self.assertLess(patch.index("gpiod_set_value_cansleep"),
                        patch.index("dev_open(ndev, NULL)"))
        self.assertEqual(patch.count("+\t\trtnl_lock();"), 2)
        self.assertEqual(patch.count("+\t\trtnl_unlock();"), 2)
        self.assertIn("netif_device_detach(ndev)", patch)
        self.assertIn("resume reinitialization failed", patch)
        include = (ROOT / "recipes-kernel/linux/h432b-suspend.inc").read_text()
        self.assertIn("0024-h432b-ethernet-full-resume.patch", include)

    def test_rtc_timekeeping_without_alarm_wake(self):
        config = (FILES / "h432b-rtc.config").read_text()
        for option in ("RTC_CLASS", "RTC_DRV_S3C", "RTC_HCTOSYS", "RTC_SYSTOHC"):
            self.assertIn("CONFIG_" + option + "=y", config)
        dt = (FILES / "s5pv210-hims-u2-suspend.dtsi").read_text()
        self.assertIn('&rtc {\n    status = "okay";', dt)
        patch = (FILES / "0025-h432b-rtc-no-alarm-wake.patch").read_text()
        self.assertIn('!of_machine_is_compatible("hims,braillesense-u2")', patch)
        self.assertIn('clock-names = "rtc", "rtc_src"', dt)
        self.assertIn("clock-frequency = <32768>", dt)
        include = (ROOT / "recipes-kernel/linux/h432b-suspend.inc").read_text()
        self.assertIn("${UNPACKDIR}/h432b-rtc.config", include)

    def test_runtime_merge_is_last(self):
        recipe = (ROOT / "recipes-kernel/linux/linux-h432b-runtime_6.12.111.bb").read_text()
        includes = [line for line in recipe.splitlines() if line.startswith("require ")]
        self.assertEqual(includes[-1], "require h432b-suspend.inc")
        self.assertIn("${UNPACKDIR}/h432b-runtime.config", recipe)
        include = (ROOT / "recipes-kernel/linux/h432b-suspend.inc").read_text()
        self.assertIn("u2-resume-test.config", include)
        self.assertIn("Automatic kernel test suspend is forbidden", include)

if __name__ == "__main__":
    unittest.main()

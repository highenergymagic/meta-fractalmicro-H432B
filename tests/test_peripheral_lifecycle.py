# SPDX-License-Identifier: MIT
"""Peripheral lifecycle contracts; hardware timing is tested on the board."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
FILES = ROOT / "recipes-kernel/linux/files"


def source(name):
    return (FILES / name).read_text()


def body(text, declaration):
    return text.split(declaration, 1)[1].split("\n}\n", 1)[0]


class PeripheralLifecycle(unittest.TestCase):
    def test_board_compatibles_identify_the_hardware_manufacturer(self):
        for driver, dt, canonical, legacy, patch in (
            ("h432b-battery-inventory.c", "battery", "hims,h432b-battery",
             "fractal,h432b-battery-inventory", "0009a-h432b-battery-binding.patch"),
            ("h432b-braille.c", "braille", "hims,h432b-braille",
             "fractal,h432b-braille", "0019a-h432b-braille-binding.patch"),
            ("h432b-gps-power.c", "gps", "hims,h432b-gps-power",
             "fractal,h432b-gps-power", "0014a-h432b-gps-binding.patch"),
        ):
            text = source(driver)
            self.assertIn(canonical, text)
            self.assertIn(legacy, text)
            self.assertNotIn("of_machine_is_compatible", text)
            tree = source("s5pv210-hims-u2-" + dt + ".dtsi")
            self.assertIn(canonical, tree)
            self.assertNotIn(legacy, tree)
            schema = source(patch)
            self.assertIn("const: " + canonical, schema)
            self.assertIn("const: " + legacy, schema)
            self.assertIn("deprecated: true", schema)

    def test_battery_stops_transactions_before_sleep(self):
        text = source("h432b-battery-inventory.c")
        stop = body(text, "static void battery_stop_poll")
        self.assertLess(stop.index("b->stopped = true"),
                        stop.index("cancel_delayed_work_sync"))
        self.assertIn("b->have_sample = false", stop)
        poll = body(text, "static void battery_poll")
        self.assertLess(poll.index("if (b->stopped)"),
                        poll.index("battery_sample_read"))
        for method in ("snapshot_show", "registers_show"):
            diagnostic = body(text, "static int " + method)
            self.assertIn("if (b->stopped)", diagnostic)
            self.assertIn("return -EBUSY", diagnostic)
        self.assertIn(".shutdown = battery_shutdown", text)
        self.assertIn(".pm = pm_sleep_ptr(&battery_pm)", text)
        self.assertIn("b->stopped = false", body(text, "static int battery_resume"))
        self.assertNotIn("of_machine_is_compatible", text)

    def test_gps_shutdown_balances_supply(self):
        text = source("h432b-gps-power.c")
        off = body(text, "static void h432b_gps_off")
        self.assertLess(off.index("gpiod_set_value_cansleep(gps->reset, 1)"),
                        off.index("regulator_disable"))
        self.assertLess(off.index("if (!gps->powered)"),
                        off.index("regulator_disable"))
        self.assertIn("gps->powered = false", off)
        self.assertIn(".shutdown = h432b_gps_shutdown", text)
        self.assertNotIn("dev_info", text)

    def test_input_and_sensor_stop_on_shutdown(self):
        self.assertIn(".shutdown = shutdown", source("h432b-input.c"))
        self.assertIn("stop(platform_get_drvdata(pdev))", source("h432b-input.c"))
        sensor = source("ami603.c")
        shutdown = body(sensor, "static void ami603_shutdown")
        self.assertIn("mutex_lock(&s->lock)", shutdown)
        self.assertIn("ami603_power_off(s)", shutdown)
        self.assertIn(".shutdown = ami603_shutdown", sensor)

    def test_braille_probe_can_establish_power(self):
        text = source("h432b-braille.c")
        probe = body(text, "static int display_probe")
        self.assertIn("gpiod_direction_output(h->enable, 0)", probe)
        self.assertLess(probe.index('devm_gpiod_get(dev, "data"'),
                        probe.index("shift_frame(h, blank)"))
        self.assertLess(probe.index("shift_frame(h, blank)"),
                        probe.index("gpiod_set_value_cansleep(h->enable, 1)"))
        self.assertNotIn("-EHOSTDOWN", probe)
        remove = body(text, "static void display_remove")
        self.assertLess(remove.index("misc_deregister"), remove.index("h->dead = true"))
        self.assertLess(remove.index("gpiod_set_value_cansleep(h->enable, 0)"),
                        remove.index("kref_put"))

    def test_driver_selection_and_subsystem_ownership(self):
        for patch, config in (("0009-battery-inventory.patch", "H432B_BATTERY"),
                              ("0010-h432b-input.patch", "KEYBOARD_H432B"),
                              ("0019-h432b-braille.patch", "H432B_BRAILLE")):
            text = source(patch)
            self.assertIn("+config " + config, text)
            self.assertIn("obj-$(CONFIG_" + config + ")", text)
            self.assertNotIn("+obj-y", text)
        include = (ROOT / "recipes-kernel/linux/h432b-battery.inc").read_text()
        self.assertIn("${S}/drivers/power/supply/", include)
        self.assertNotIn("must not enable suspend", include)

    def test_fm_resume_and_shutdown_contract(self):
        patch = source("0018-h432b-fm-radio.patch")
        self.assertIn("+\tretval = si470x_set_freq", patch)
        self.assertIn("+\tradio->registers[POWERCFG] &= ~POWERCFG_DISABLE", patch)
        self.assertIn("+\tif (!radio->resume_enabled)", patch)
        self.assertIn("+\t\tret = si470x_start(radio)", patch)
        self.assertIn(".shutdown\t\t= si470x_i2c_shutdown", patch)
        self.assertIn("+\tmutex_lock(&radio->lock)", patch)


if __name__ == "__main__":
    unittest.main()

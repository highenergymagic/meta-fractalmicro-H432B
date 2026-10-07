# SPDX-License-Identifier: MIT
"""FM identification contracts; no claim of tuner emulation."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
FILES = ROOT / "recipes-kernel/linux/files"

class FM(unittest.TestCase):
    def test_pins_do_not_overlap_codec_or_pmic(self):
        dt = (FILES / "s5pv210-hims-u2-fm.dtsi").read_text()
        for item in ("<&gpd1 0 ", "<&gpd1 1 ", "<&gpe0 4 ",
                     "<&gph3 2 GPIO_ACTIVE_LOW>", "<&gph1 4 "):
            self.assertIn(item, dt)
        self.assertNotIn("&gpd0", dt)
        self.assertNotIn("<&gpd1 4 ", dt)
        self.assertIn("reg = <0x10>", dt)

    def test_identity_muting_polling_and_reset_cleanup(self):
        src = (FILES / "h432b-fm.c").read_text()
        self.assertIn("radio->registers[DEVICEID] != 0x1242", src)
        self.assertIn("radio->registers[SI_CHIPID] != 0x1000", src)
        self.assertIn("msecs_to_jiffies(1000)", src)
        self.assertIn("return -ETIMEDOUT", src)
        self.assertIn("(mute->val ? 0 : POWERCFG_DMUTE)", src)
        self.assertIn("V4L2_CAP_TUNER | V4L2_CAP_RADIO", src)
        self.assertIn("devm_add_action_or_reset", src)
        self.assertIn("gpiod_set_value_cansleep(fm->reset, 1)", src)
        self.assertIn("gpiod_set_value_cansleep(fm->enable, 0)", src)

    def test_runtime_includes_bus(self):
        self.assertIn('s5pv210-hims-u2-fm.dtsi',
                      (FILES / "s5pv210-hims-u2-runtime.dts").read_text())
        self.assertIn("require h432b-fm.inc", (ROOT /
                      "recipes-kernel/linux/linux-h432b-runtime_6.12.111.bb").read_text())

    def test_listening_is_explicit_bounded_and_remuted(self):
        src = (ROOT / "recipes-support/h432b-fm-check/files/fm-check.c").read_text()
        self.assertIn('argc == 2 && !strcmp(argv[1], "--listen")', src)
        self.assertIn("listen && !interrupted", src)
        self.assertIn("i < 100 && !interrupted", src)
        self.assertIn(".tv_nsec = 100000000", src)
        self.assertIn("sigaction(SIGTERM", src)
        self.assertIn("sigaction(SIGHUP", src)
        self.assertIn("out:\n\tmute.value = 1;", src)

    def test_scan_is_explicit_and_checks_muting(self):
        src = (ROOT / "recipes-support/h432b-fm-check/files/fm-check.c").read_text()
        self.assertIn('argc == 2 && !strcmp(argv[1], "--scan")', src)
        self.assertIn("scan ? 206", src)
        self.assertIn("87500 + i * 100", src)
        self.assertIn(".tv_nsec = 200000000", src)
        self.assertIn("set.frequency != got.frequency || mute.value != 1", src)
        self.assertIn("if (interrupted) goto out;", src)

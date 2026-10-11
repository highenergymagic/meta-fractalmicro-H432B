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
                     "<&gph3 2 GPIO_ACTIVE_LOW>", "&gph1", "gpios = <4 GPIO_ACTIVE_HIGH>"):
            self.assertIn(item, dt)
        self.assertNotIn("&gpd0", dt)
        self.assertNotIn("<&gpd1 4 ", dt)
        self.assertIn("reg = <0x10>", dt)

    def test_identity_muting_polling_and_reset_cleanup(self):
        src = (FILES / "0018-h432b-fm-radio.patch").read_text()
        self.assertIn("radio->registers[DEVICEID] != 0x1242", src)
        self.assertIn("radio->registers[SI_CHIPID] != 0x1000", src)
        self.assertIn("msecs_to_jiffies(timeout)", src)
        self.assertIn("return -ETIMEDOUT", src)
        self.assertIn("(radio->mute->val ? 0 : POWERCFG_DMUTE)", src)
        self.assertIn("V4L2_CAP_TUNER | V4L2_CAP_RADIO", src)
        self.assertIn("devm_add_action_or_reset", src)
        self.assertIn("gpiod_set_value_cansleep(radio->gpio_reset, 1)", src)
        self.assertIn("regulator_disable(radio->vdd)", src)
        self.assertNotIn("start_board", src)
        self.assertNotIn("tune_board", src)
        self.assertNotIn("h432b-fm.c", src)
        self.assertNotIn("hims,h432b-si4702", src)
        self.assertIn("silabs,si4702-c19", src)

    def test_open_file_state_outlives_i2c_unbind(self):
        patch = (FILES / "0018-h432b-fm-radio.patch").read_text()
        added = "\n".join(line[1:] for line in patch.splitlines()
                          if line.startswith("+") and not line.startswith("+++"))
        for expected in ("radio = kzalloc(sizeof(*radio), GFP_KERNEL)",
                         "radio->v4l2_dev.release = si470x_i2c_release",
                         "devm_add_action_or_reset(&client->dev, si470x_put_device, radio)",
                         "v4l2_device_put(&radio->v4l2_dev)",
                         "v4l2_device_disconnect(&radio->v4l2_dev)",
                         "video_is_registered(&radio->videodev) && v4l2_fh_is_singular_file(file)"):
            self.assertIn(expected, added)
        # ioctl2 takes videodev.lock before the control handler takes its lock.
        self.assertNotIn("radio->hdl.lock = &radio->lock", added)

    def test_si4702_does_not_use_si4703_firmware_threshold(self):
        patch = (FILES / "0018-h432b-fm-radio.patch").read_text()
        added = "\n".join(line[1:] for line in patch.splitlines()
                          if line.startswith("+") and not line.startswith("+++"))
        self.assertIn("if (!radio->is_si4702 &&\n"
                      "\t    (radio->registers[SI_CHIPID] & SI_CHIPID_FIRMWARE) "
                      "< RADIO_FW_VERSION)", added)

    def test_power_and_regional_settings_are_described_by_dt(self):
        dt = (FILES / "s5pv210-hims-u2-fm.dtsi").read_text()
        self.assertIn("vdd-supply = <&fm_supply>", dt)
        self.assertIn("silabs,channel-spacing-khz = <100>", dt)
        self.assertIn("silabs,deemphasis-us = <50>", dt)
        self.assertIn("silabs,use-crystal", dt)
        self.assertIn("gpio-hog", dt)
        self.assertNotIn("mode-gpios", dt)
        patch = (FILES / "0018-h432b-fm-radio.patch").read_text()
        for expected in ("case 200:", "case 100:", "case 50:",
                         "value != 50 && value != 75",
                         "if (!radio->vdd_enabled)", "radio->vdd_enabled = false"):
            self.assertIn(expected, patch)
        self.assertIn("silabs,si4702.yaml", (FILES / "0018a-si4702-binding.patch").read_text())

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

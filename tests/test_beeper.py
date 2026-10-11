# SPDX-License-Identifier: MIT
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
FILES = ROOT / "recipes-kernel/linux/files"

class BeeperTests(unittest.TestCase):
    def test_passive_beeper_uses_standard_pwm_driver(self):
        dts = (FILES / "s5pv210-hims-u2-beeper.dtsi").read_text()
        for value in ('compatible = "pwm-beeper"', 'pwms = <&pwm 0 1000000 0>',
                      'samsung,pwm-outputs = <0>', 'samsung,pins = "gpd0-0"',
                      'samsung,pin-function = <2>'):
            self.assertIn(value, dts)

    def test_runtime_integration(self):
        self.assertIn('s5pv210-hims-u2-beeper.dtsi',
                      (FILES / "s5pv210-hims-u2-runtime.dts").read_text())
        self.assertIn('require h432b-beeper.inc',
                      (FILES.parent / "linux-h432b-runtime_6.12.111.bb").read_text())
        config = (FILES / "h432b-beeper.config").read_text()
        self.assertIn('CONFIG_INPUT_PWM_BEEPER=y', config)
        self.assertIn('CONFIG_PWM_SAMSUNG=y', config)

    def test_disabled_apply_handles_inherited_pwm_and_resume_mask(self):
        patch = (FILES / "0032-pwm-samsung-honor-disabled-state.patch").read_text()
        self.assertIn('-\t\tif (enabled)', patch)
        self.assertIn('+\t\tpwm_samsung_disable(chip, pwm);', patch)
        self.assertIn('disabled_mask', patch)
        self.assertIn('0032-pwm-samsung-honor-disabled-state.patch',
                      (FILES.parent / "h432b-beeper.inc").read_text())

if __name__ == "__main__":
    unittest.main()

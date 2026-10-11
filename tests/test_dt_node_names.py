# SPDX-License-Identifier: MIT
"""Keep board node names compatible with the upstream controller bindings."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
FILES = ROOT / 'recipes-kernel/linux/files'


class DeviceTreeNodeNames(unittest.TestCase):
    def test_pinctrl_labels_keep_binding_compliant_node_names(self):
        labels = {
            's5pv210-hims-u2.dts': ['ethernet_irq'],
            's5pv210-hims-u2-battery.dtsi': ['battery_inputs', 'battery_output'],
            's5pv210-hims-u2-beeper.dtsi': ['h432b_beeper_pin'],
            's5pv210-hims-u2-bluetooth.dtsi': ['bluetooth_enable_pin'],
            's5pv210-hims-u2-gps.dtsi': [
                'gps_reset_pin', 'gps_enable_pin', 'gps_uart_tx_pin', 'gps_uart_rx_pin'],
            's5pv210-hims-u2-usb-host-test.dtsi': [
                'usb_hub_enable_pin', 'usb12_enable_pin', 'usb3_enable_pin',
                'usb_hub_reset_pin'],
            's5pv210-hims-u2-suspend.dtsi': ['wifi_power_sleep', 'braille_power_sleep'],
            's5pv210-hims-u2-external-sd.dtsi': ['external_sd_detect'],
        }
        for filename, names in labels.items():
            source = (FILES / filename).read_text()
            for label in names:
                with self.subTest(file=filename, label=label):
                    match = re.search(r'\b' + label + r':\s*([a-z0-9-]+)\s*\{', source)
                    self.assertIsNotNone(match)
                    self.assertRegex(match.group(1), r'^[a-z0-9-]+-pins$')

    def test_bus_labels_and_pmic_alias_are_preserved(self):
        self.assertIn('codec_control: spi {', (FILES / 's5pv210-hims-u2.dts').read_text())
        pmic = (FILES / 's5pv210-hims-u2-pmic-bus.dtsi').read_text()
        self.assertIn('pmic_inventory_bus: i2c-pmic {', pmic)
        self.assertIn('i2c9 = &pmic_inventory_bus;', pmic)

    def test_gpio_hogs_require_the_standard_binding(self):
        patch = (FILES / '0040-samsung-gpio-hog-binding.patch').read_text()
        self.assertIn('"^.+-hog(-[0-9]+)?$"', patch)
        self.assertIn('+      - gpio-hog', patch)
        self.assertNotIn('+additionalProperties: true', patch)


if __name__ == '__main__':
    unittest.main()

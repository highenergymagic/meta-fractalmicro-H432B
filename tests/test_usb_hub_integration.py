# SPDX-License-Identifier: MIT
from pathlib import Path
import unittest
ROOT=Path(__file__).resolve().parents[1]
FILES=ROOT/"recipes-kernel/linux/files"

class UsbHubIntegrationTests(unittest.TestCase):
    def test_gpio_scope_and_polarity(self):
        text=(FILES/"s5pv210-hims-u2-usb-host-test.dtsi").read_text()
        self.assertIn('compatible = "usb409,5a"',text)
        self.assertIn("reset-gpios = <&gpj4 2 GPIO_ACTIVE_LOW>",text)
        for bank,pin in (("gph2",7),("gph3",7),("gph1",3)):
            self.assertIn("gpio = <&%s %d GPIO_ACTIVE_HIGH>"%(bank,pin),text)
        self.assertEqual(text.count("samsung,pin-pud = <0>"),4)
        for forbidden in ("&gpj4 1","&gpj0","&gpe1","pmic","regulator-min-microvolt"):
            self.assertNotIn(forbidden,text)
        self.assertIn("vdd-supply = <&usb_hub_supply>",text)
        self.assertEqual(text.count("regulator-always-on;"),2)

    def test_reset_has_no_duplicate_mux_claim(self):
        text=(FILES/"s5pv210-hims-u2-usb-host-test.dtsi").read_text()
        reset=text.split("usb_hub_reset_pin: usb-hub-reset-pins {",1)[1].split("};",1)[0]
        self.assertIn('samsung,pins = "gpj4-2"',reset)
        self.assertIn("samsung,pin-pud = <0>",reset)
        self.assertNotIn("samsung,pin-function",reset)

    def test_framework_and_timing(self):
        text=(FILES/"0012-onboard-h432b-usb-hub.patch").read_text()
        self.assertIn(".reset_us = 100000",text)
        self.assertIn('.compatible = "usb409,5a"',text)
        self.assertIn("USB_DEVICE(VENDOR_ID_NEC, 0x005a)",text)
        self.assertIn('.supply_names = { "vdd" }',text)
        self.assertIn(".is_hub = true",text)

    def test_shared_builtin_driver(self):
        config=(FILES/"u2-usb-host-test.config").read_text()
        for option in ("USB_ONBOARD_DEV","REGULATOR_FIXED_VOLTAGE","USB_SERIAL_PL2303"):
            self.assertIn("CONFIG_"+option+"=y",config)
        self.assertIn("usb-host-test.dtsi",(FILES/"s5pv210-hims-u2-resume-test.dts").read_text())
        self.assertNotIn("usb-host-test.dtsi",(FILES/"s5pv210-hims-u2.dts").read_text())
        recipe=(ROOT/"recipes-kernel/linux/linux-h432b-resume-test_6.12.111.bb").read_text()
        self.assertIn("require h432b-usb-host.inc",recipe)
        shared=(ROOT/"recipes-kernel/linux/h432b-usb-host.inc").read_text()
        self.assertIn("0012-onboard-h432b-usb-hub.patch",shared)

if __name__=="__main__":
    unittest.main()

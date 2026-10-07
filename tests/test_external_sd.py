# SPDX-License-Identifier: MIT
from pathlib import Path
import unittest
ROOT=Path(__file__).resolve().parents[1]
FILES=ROOT/"recipes-kernel/linux/files"

class ExternalSdTests(unittest.TestCase):
    def test_controller_and_detect(self):
        text=(FILES/"s5pv210-hims-u2-external-sd.dtsi").read_text()
        for expected in ("&sdhci2", "hims,read-only-probe", "max-frequency = <25000000>",
                         "cd-gpios = <&gph3 1 GPIO_ACTIVE_LOW>", 'samsung,pins = "gph3-1"',
                         "samsung,pin-pud = <0>", "no-1-8-v", "&sd2_bus4"):
            self.assertIn(expected,text)
        for forbidden in ("/delete-property/", "non-removable", "broken-cd", "regulator-always-on"):
            self.assertNotIn(forbidden,text)
        diagnostic=(FILES/"s5pv210-hims-u2-external-sd-test.dts").read_text()
        self.assertIn('#include "s5pv210-hims-u2-external-sd.dtsi"',diagnostic)

    def test_nand_profile(self):
        recipe=(ROOT/"recipes-kernel/linux/linux-h432b-external-sd-test_6.12.111.bb").read_text()
        self.assertIn('d.getVar("H432B_NAND_PROFILE") != "readonly"',recipe)

    def test_read_only_test(self):
        text=(ROOT/"tests/check-external-sd.sh").read_text()
        for guard in ("eb100000.mmc", "eb200000.mmc", "hims,external-sd-read-test",
                      'test "$(cat "$candidate/ro")" = 1', "iflag=direct", "sha256sum"):
            self.assertIn(guard,text)
        for forbidden in ("of=/dev/", 'of="/dev/', "mkfs", "mount ", "wipefs"):
            self.assertNotIn(forbidden,text)

if __name__=="__main__":
    unittest.main()
